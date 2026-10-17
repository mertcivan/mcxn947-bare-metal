/*
 * FRDM-MCXN947 - Milestone 1: clock bring-up to 150 MHz and its proof.
 * References: MCX N Reference Manual, Rev. 7, 2025-10-10.
 */
#include "fsl_device_registers.h"
#include "drvPLL.h"

/* ---------------------------------------------------------------------------
 * Reset-state survey.
 *
 * Captured before anything is configured. Globals, volatile, never static:
 * the Makefile builds with -ffunction-sections -fdata-sections and links with
 * --gc-sections, which would otherwise discard a variable that is only ever
 * written. Read these in the debugger; there is no UART yet.
 * ------------------------------------------------------------------------- */
volatile uint32_t rst_scg_param;        /* which sources this silicon has     */
volatile uint32_t rst_scg_csr;          /* CSR[SCS] - expect 3 = FIRC         */
volatile uint32_t rst_scg_rccr;
volatile uint32_t rst_scg_firccsr;
volatile uint32_t rst_scg_sosccsr;      /* LK bit must be clear               */
volatile uint32_t rst_scg_apllcsr;      /* LK bit must be clear               */
volatile uint32_t rst_scg_ldocsr;
volatile uint32_t rst_syscon_clkunlock; /* 0 = clock registers writable       */
volatile uint32_t rst_syscon_ahbclkdiv;
volatile uint32_t rst_spc_active_cfg;   /* reset core voltage level           */
volatile uint32_t rst_fmu_fctrl;        /* reset flash wait states            */

/* Decoded for convenience in the watch window. */
volatile uint32_t rst_scs;              /* expect 3 (FIRC)                    */
volatile uint32_t rst_coreldo_lvl;      /* 1=MID 1.0V 2=NORMAL 1.1V 3=OD 1.2V */
volatile uint32_t rst_rwsc;             /* expect 3 per RM Table 37           */

/* Results, filled in after bring-up. */
volatile uint32_t run_status;           /* drvPLL_Status_t                    */
volatile uint32_t run_core_hz;
volatile uint32_t run_scs;              /* expect 5 (APLL) on success         */

static void survey_reset_state(void)
{
    rst_scg_param        = SCG0->PARAM;
    rst_scg_csr          = SCG0->CSR;
    rst_scg_rccr         = SCG0->RCCR;
    rst_scg_firccsr      = SCG0->FIRCCSR;
    rst_scg_sosccsr      = SCG0->SOSCCSR;
    rst_scg_apllcsr      = SCG0->APLLCSR;
    rst_scg_ldocsr       = SCG0->LDOCSR;
    rst_syscon_clkunlock = SYSCON0->CLKUNLOCK;
    rst_syscon_ahbclkdiv = SYSCON0->AHBCLKDIV;
    rst_spc_active_cfg   = SPC0->ACTIVE_CFG;
    rst_fmu_fctrl        = FMU0->FCTRL;

    rst_scs         = (rst_scg_csr & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT;
    rst_coreldo_lvl = (rst_spc_active_cfg & SPC_ACTIVE_CFG_CORELDO_VDD_LVL_MASK)
                      >> SPC_ACTIVE_CFG_CORELDO_VDD_LVL_SHIFT;
    rst_rwsc        = (rst_fmu_fctrl & FMU_FCTRL_RWSC_MASK) >> FMU_FCTRL_RWSC_SHIFT;
}

/* ---------------------------------------------------------------------------
 * 150 MHz from the internal 48 MHz FIRC.
 *
 * RM Table 326 (p.1224):  Fref = Fin/N,  Fcco = M*Fref,  Fout = Fcco/(2*P)
 *
 *   Fref = 48 MHz / 4  =  12 MHz   (RM wants Fref > 3 MHz)
 *   Fcco = 25 * 12 MHz = 300 MHz   (RM wants 275..550 MHz)
 *   Fout = 300 MHz / 2 = 150 MHz   (P = 1, divide-by-2 in circuit)
 *
 * FIRC is chosen for the first bring-up because its frequency is known and
 * guaranteed; the board crystal has not been measured yet. Switching to SOSC
 * later changes only `source`, `srcFreqHz`, `soscRange` and `ndiv`.
 * ------------------------------------------------------------------------- */
static const drvPLL_Config_t clk150_from_firc = {
    .source         = DRVPLL_SRC_FIRC,
    .srcFreqHz      = 48000000u,
    .soscRange      = DRVPLL_SOSC_20_30MHZ,  /* unused for FIRC */
    .soscIsCrystal  = true,                  /* unused for FIRC */
    .ndiv           = 4u,
    .mdiv           = 25u,
    .pdiv           = 1u,
    .bypassPostDiv2 = false,
    .ahbDiv         = 0u,                    /* system_clk = main_clk / 1 */
    .mode           = DRVPLL_MODE_OD,        /* 1.2 V, required above 100 MHz */
};

/* Same 150 MHz from a 24 MHz crystal, once the board value is confirmed.
 * Fref = 24/2 = 12 MHz, so M, P and the loop filter are identical.
 *
 * static const drvPLL_Config_t clk150_from_sosc = {
 *     .source = DRVPLL_SRC_SOSC, .srcFreqHz = 24000000u,
 *     .soscRange = DRVPLL_SOSC_20_30MHZ, .soscIsCrystal = true,
 *     .ndiv = 2u, .mdiv = 25u, .pdiv = 1u, .bypassPostDiv2 = false,
 *     .ahbDiv = 0u, .mode = DRVPLL_MODE_OD,
 * };
 */

/* ---------------------------------------------------------------------------
 * CLKOUT measurement tap.
 *
 * RM Table 264 (p.479): CLKOUT is available on PIO0_6, PIO2_2 and PIO3_6.
 * P2_2 is ALT1 = CLKOUT (RM p.290). Confirm against the FRDM schematic that
 * the pin you pick reaches a header before probing.
 *
 * RM Table 320 (p.1199): CLKOUT itself tops out at 100 MHz in OD mode, so
 * 150 MHz cannot be driven out raw - the divider below is mandatory, not a
 * concession to the logic analyser.
 * ------------------------------------------------------------------------- */
#define CLKOUT_PORT   PORT2
#define CLKOUT_PIN    (2u)
#define CLKOUT_MUX    (1u)   /* ALT1 */
#define CLKOUT_GATE   SYSCON_AHBCLKCTRL0_PORT2_MASK

/* Change these in the debugger to walk the verification ladder without
 * reflashing. RM 17.4.1 CLKOUTSEL: 0=main_clk 1=pll0_clk(APLL) 2=clk_in(SOSC)
 * 3=fro_hf(FIRC) 4=fro_12m. Divider is "divide by (DIV+1)". */
volatile uint32_t clkout_sel = 0u;    /* main_clk - what the core really runs on */
volatile uint32_t clkout_div = 149u;  /* 150 MHz / 150 = 1 MHz => 24 samples/period */

static void clkout_pin_init(void)
{
    /* A peripheral whose gate is closed ignores register writes silently. */
    SYSCON0->AHBCLKCTRL0 |= CLKOUT_GATE;

    /* MUX picks the function. DSE/SRE matter because this pin carries MHz:
     * normal drive strength, fast slew. IBE is for inputs, so it stays off. */
    CLKOUT_PORT->PCR[CLKOUT_PIN] = PORT_PCR_MUX(CLKOUT_MUX) | PORT_PCR_DSE_MASK;
}

static void clkout_apply(void)
{
    SYSCON0->CLKOUTDIV = SYSCON_CLKOUTDIV_DIV(clkout_div);
    SYSCON0->CLKOUTSEL = SYSCON_CLKOUTSEL_SEL(clkout_sel);
}

int main(void)
{
    survey_reset_state();

    run_status  = (uint32_t)drvPLL_Init(&clk150_from_firc);
    run_core_hz = drvPLL_GetCoreClockHz();
    run_scs     = (SCG0->CSR & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT;

    clkout_pin_init();

    for (;;) {
        /* Re-apply every pass so edits to clkout_sel / clkout_div made from
         * the debugger take effect immediately. */
        clkout_apply();
    }
}
