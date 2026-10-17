/*
 * drvPLL - MCXN947 clock tree bring-up.
 * All references: MCX N Reference Manual, Rev. 7, 2025-10-10.
 */
#include <stddef.h>

#include "drvPLL.h"
#include "fsl_device_registers.h"

/* Generous bound for every hardware poll. At the 48 MHz reset clock each
 * iteration is a handful of cycles, so this is tens of milliseconds - far
 * longer than any documented settling time, short enough to never hang. */
#define DRVPLL_TIMEOUT  (1000000u)

/* RM Table 326 (p.1224): Fcco must stay inside 275..550 MHz. */
#define FCCO_MIN_HZ     (275000000u)
#define FCCO_MAX_HZ     (550000000u)
/* RM 34.6 step 5 (p.1237): Fout lower bound. */
#define FOUT_MIN_HZ     (4300000u)
/* RM 34.3.4.4.3.2 (p.1227): "Preferred reference frequency Fref > 3 MHz". */
#define FREF_MIN_HZ     (3000000u)

/* RM 34.7 TRIM_LOCK: key 5A5Ah in [31:16] plus TRIM_UNLOCK in [0].
 * RM p.1241 step 3n writes exactly this to open APLLLOCK_CNFG. */
#define TRIM_UNLOCK_KEY (0x5A5A0001u)

/* RM 34.7 RCCR[SCS]: 5 = APLL. (6 would be SPLL = PLL1.) */
#define SCS_APLL        (5u)

static uint32_t s_coreClockHz;

/* Poll expr until true. Evaluates to false on timeout. */
#define WAIT_FOR(expr)                                   \
    __extension__ ({                                     \
        uint32_t _t = DRVPLL_TIMEOUT;                    \
        while (!(expr) && (_t != 0u)) { _t--; }          \
        (_t != 0u);                                      \
    })

/* ---------------------------------------------------------------------------
 * Loop filter coefficients - RM Equation 1 and 2 (p.1227).
 * Valid only while APLLCTRL[BANDDIRECT] = 0, which is what we use.
 * ------------------------------------------------------------------------- */

/* SELP: A = floor(M/4) + 1; SELP = A if A < 31 else 31. */
static uint32_t calc_selp(uint32_t m)
{
    uint32_t a = (m / 4u) + 1u;
    return (a < 31u) ? a : 31u;
}

/* SELI: M >= 8000        -> A = 1
 *       8000 > M >= 122  -> A = floor(8000/M)
 *       122  > M >= 1    -> A = 2*floor(M/4) + 3
 *       SELI = A if A < 63 else 63. */
static uint32_t calc_seli(uint32_t m)
{
    uint32_t a;
    if (m >= 8000u)     { a = 1u; }
    else if (m >= 122u) { a = 8000u / m; }
    else                { a = (2u * (m / 4u)) + 3u; }
    return (a < 63u) ? a : 63u;
}

/* RM p.1241 step 3o: LOCK_TIME = 500 us / T(clk_ref) + 300, where clk_ref is
 * Fref = Fin/N. 500us/T == 500e-6 * Fref == Fref/2000. */
static uint32_t calc_lock_time(uint32_t frefHz)
{
    return (frefHz / 2000u) + 300u;
}

/* ---------------------------------------------------------------------------
 * Core voltage - SPC. RM p.1316 maps modes to volts, RM p.1398 gives the rules.
 * ------------------------------------------------------------------------- */
static drvPLL_Status_t set_core_voltage(drvPLL_Mode_t mode)
{
    uint32_t cfg = SPC0->ACTIVE_CFG;

    /* RM p.1398: "LDO_CORE overdrive voltage level (1.2V) is not supported
     * when LDO_CORE is configured for low drive." -> force normal drive.
     * Same page: "When setting CORELDO_VDD_DS to Normal, BGMODE must be
     * programmed to a value that enables the Bandgap." */
    cfg &= ~(SPC_ACTIVE_CFG_CORELDO_VDD_LVL_MASK |
             SPC_ACTIVE_CFG_DCDC_VDD_LVL_MASK    |
             SPC_ACTIVE_CFG_BGMODE_MASK);
    cfg |= SPC_ACTIVE_CFG_CORELDO_VDD_DS(1u);
    cfg |= SPC_ACTIVE_CFG_BGMODE(1u);

    /* RM p.1398: "You must specify the same level for both CORELDO_VDD_LVL
     * and DCDC_VDD_LVL, even if LDO_CORE is off." */
    cfg |= SPC_ACTIVE_CFG_CORELDO_VDD_LVL((uint32_t)mode);
    cfg |= SPC_ACTIVE_CFG_DCDC_VDD_LVL((uint32_t)mode);

    SPC0->ACTIVE_CFG = cfg;

    /* RM 37.7 SC[BUSY]: the regulator change is in flight until this clears.
     * Raising the clock before the rail has settled is what browns out. */
    if (!WAIT_FOR((SPC0->SC & SPC_SC_BUSY_MASK) == 0u)) {
        return DRVPLL_ERR_SPC_TIMEOUT;
    }
    return DRVPLL_OK;
}

/* ---------------------------------------------------------------------------
 * Flash wait states - FMU. RM Table 37 (p.91).
 *
 *   RWSC  OD mode          SD mode          MD mode
 *   0011  101-150 MHz      -                -          <- reset default
 *   0010  65-100 MHz       65-100 MHz       -
 *   0001  37-64 MHz        37-64 MHz        25-50 MHz
 *   0000  <= 36 MHz        <= 36 MHz        <= 24 MHz
 *
 * The reset default is already the slowest/safest setting, so going up in
 * frequency usually needs no change at all. We still compute the requirement
 * and only ever RAISE it - lowering it before the clock has dropped would
 * make flash unreadable mid-function.
 * ------------------------------------------------------------------------- */
static uint32_t required_rwsc(uint32_t sysClkHz, drvPLL_Mode_t mode)
{
    if (mode == DRVPLL_MODE_MD) {
        if (sysClkHz <= 24000000u) { return 0u; }
        return 1u;                              /* 25-50 MHz */
    }
    /* OD and SD share the same breakpoints up to 100 MHz. */
    if (sysClkHz <=  36000000u) { return 0u; }
    if (sysClkHz <=  64000000u) { return 1u; }
    if (sysClkHz <= 100000000u) { return 2u; }
    return 3u;                                  /* 101-150 MHz, OD only */
}

static void raise_flash_waitstates(uint32_t sysClkHz, drvPLL_Mode_t mode)
{
    uint32_t need = required_rwsc(sysClkHz, mode);
    uint32_t cur  = (FMU0->FCTRL & FMU_FCTRL_RWSC_MASK) >> FMU_FCTRL_RWSC_SHIFT;

    if (need > cur) {
        FMU0->FCTRL = (FMU0->FCTRL & ~FMU_FCTRL_RWSC_MASK) | FMU_FCTRL_RWSC(need);
    }
}

/* ---------------------------------------------------------------------------
 * PLL LDO - RM 34.3.4.7 step 1: "Enable PLL LDO."
 * ------------------------------------------------------------------------- */
static drvPLL_Status_t enable_pll_ldo(void)
{
    SCG0->LDOCSR |= SCG_LDOCSR_LDOEN_MASK;

    if (!WAIT_FOR((SCG0->LDOCSR & SCG_LDOCSR_VOUT_OK_MASK) != 0u)) {
        return DRVPLL_ERR_LDO_TIMEOUT;
    }
    return DRVPLL_OK;
}

/* ---------------------------------------------------------------------------
 * Reference source. RM 34.3.4.7 step 5: wait for SOSCCSR[SOSCVLD] (SOSC) or
 * FIRCCSR[FIRCACC] (FIRC) before powering the PLL.
 * ------------------------------------------------------------------------- */
static drvPLL_Status_t enable_source(const drvPLL_Config_t *cfg)
{
    if (cfg->source == DRVPLL_SRC_SOSC) {
        /* RM p.1240 step 2c: LK must be clear or SOSCCSR writes are dropped. */
        if ((SCG0->SOSCCSR & SCG_SOSCCSR_LK_MASK) != 0u) {
            return DRVPLL_ERR_REG_LOCKED;
        }

        /* RM 34.7 SOSCCFG: EREFS 1 = internal crystal oscillator,
         * 0 = external reference clock. RANGE picks the band. */
        SCG0->SOSCCFG = SCG_SOSCCFG_RANGE((uint32_t)cfg->soscRange) |
                        (cfg->soscIsCrystal ? SCG_SOSCCFG_EREFS_MASK : 0u);

        SCG0->SOSCCSR = SCG_SOSCCSR_SOSCEN_MASK;

        /* A crystal that is absent or mis-ranged never asserts SOSCVLD. This
         * is why the wait is bounded: otherwise the board hangs here and the
         * debugger cannot get in. */
        if (!WAIT_FOR((SCG0->SOSCCSR & SCG_SOSCCSR_SOSCVLD_MASK) != 0u)) {
            return DRVPLL_ERR_SRC_TIMEOUT;
        }
        if ((SCG0->SOSCCSR & SCG_SOSCCSR_SOSCERR_MASK) != 0u) {
            return DRVPLL_ERR_SRC_ERROR;
        }
    } else {
        /* FIRC is already running - it is the reset clock (RM 34.3.2). We only
         * make sure the 48 MHz peripheral tap the PLL uses is enabled; the SVD
         * notes "FIRC_SCLK_PERIPH_EN must be set to use FIRC 48 MHz clock". */
        SCG0->FIRCCSR |= SCG_FIRCCSR_FIRCEN_MASK |
                         SCG_FIRCCSR_FIRC_SCLK_PERIPH_EN_MASK;

        /* RM 34.3.4.7 step 5 names FIRCACC - accuracy - not FIRCVLD, for FIRC. */
        if (!WAIT_FOR((SCG0->FIRCCSR & SCG_FIRCCSR_FIRCACC_MASK) != 0u)) {
            return DRVPLL_ERR_SRC_TIMEOUT;
        }
    }
    return DRVPLL_OK;
}

/* ---------------------------------------------------------------------------
 * APLL (= PLL0, RM Figure 127).
 * ------------------------------------------------------------------------- */
static drvPLL_Status_t configure_apll(const drvPLL_Config_t *cfg, uint32_t frefHz)
{
    if ((SCG0->APLLCSR & SCG_APLLCSR_LK_MASK) != 0u) {
        return DRVPLL_ERR_REG_LOCKED;
    }

    /* RM p.1279: "you must program the divider ratio of the different dividers
     * (MDIV, NDIV, PDIV) when PLL is in reset state (APLLPWREN = 0)". The
     * NREQ/MREQ/PREQ handshake is only for changing ratios on-the-fly; with
     * more than one divider changing, the RM explicitly says use this method. */
    SCG0->APLLCSR &= ~(SCG_APLLCSR_APLLPWREN_MASK | SCG_APLLCSR_APLLCLKEN_MASK);

    /* SELR/SELI/SELP derive from M (RM Equation 1 and 2); SELR is always 0.
     * BANDDIRECT stays 0 so the bandwidth tracks M, as those equations assume. */
    SCG0->APLLCTRL = SCG_APLLCTRL_SOURCE((uint32_t)cfg->source)   |
                     SCG_APLLCTRL_SELR(0u)                       |
                     SCG_APLLCTRL_SELI(calc_seli(cfg->mdiv))     |
                     SCG_APLLCTRL_SELP(calc_selp(cfg->mdiv))     |
                     (cfg->bypassPostDiv2 ? SCG_APLLCTRL_BYPASSPOSTDIV2_MASK : 0u);
    /* BYPASSPREDIV and BYPASSPOSTDIV left at 0 => both N and P are in circuit. */

    /* RM 34.7: "Do not write 0 to this field" (NDIV). Validated by the caller. */
    SCG0->APLLNDIV = SCG_APLLNDIV_NDIV(cfg->ndiv);
    SCG0->APLLMDIV = SCG_APLLMDIV_MDIV(cfg->mdiv);
    SCG0->APLLPDIV = SCG_APLLPDIV_PDIV(cfg->pdiv);

    /* RM p.1241 step 3n: APLLLOCK_CNFG is behind the trim lock. Without this
     * write the LOCK_TIME store is silently discarded. */
    SCG0->TRIM_LOCK = TRIM_UNLOCK_KEY;
    SCG0->APLLLOCK_CNFG = SCG_APLLLOCK_CNFG_LOCK_TIME(calc_lock_time(frefHz));

    /* RM 34.3.4.7 step 6: power up and ungate. "The PLL clock is gated until
     * PLL_LOCK = 1", so enabling the output early is safe. */
    SCG0->APLLCSR |= SCG_APLLCSR_APLLPWREN_MASK | SCG_APLLCSR_APLLCLKEN_MASK;

    if (!WAIT_FOR((SCG0->APLLCSR & SCG_APLLCSR_APLL_LOCK_MASK) != 0u)) {
        return DRVPLL_ERR_LOCK_TIMEOUT;
    }
    if ((SCG0->APLLCSR & SCG_APLLCSR_APLLERR_MASK) != 0u) {
        return DRVPLL_ERR_PLL_ERROR;
    }
    return DRVPLL_OK;
}

/* ------------------------------------------------------------------------- */

uint32_t drvPLL_CalcOutputHz(const drvPLL_Config_t *cfg)
{
    if ((cfg == NULL) || (cfg->ndiv == 0u) || (cfg->pdiv == 0u) ||
        (cfg->mdiv == 0u) || (cfg->srcFreqHz == 0u)) {
        return 0u;
    }

    /* RM Table 326 (p.1224). 64-bit math: M * Fref overflows 32 bits easily. */
    uint64_t fref = (uint64_t)cfg->srcFreqHz / cfg->ndiv;
    uint64_t fcco = fref * cfg->mdiv;
    uint64_t fout = cfg->bypassPostDiv2 ? (fcco / cfg->pdiv)
                                        : (fcco / (2u * (uint32_t)cfg->pdiv));

    /* RM 34.6 step 5 (p.1237) - every documented constraint, checked. */
    if (fref < FREF_MIN_HZ)                           { return 0u; }
    if ((fcco < FCCO_MIN_HZ) || (fcco > FCCO_MAX_HZ)) { return 0u; }
    if (fout < FOUT_MIN_HZ)                           { return 0u; }
    if (cfg->pdiv > 31u)                              { return 0u; }

    return (uint32_t)fout;
}

drvPLL_Status_t drvPLL_Init(const drvPLL_Config_t *cfg)
{
    if (cfg == NULL) {
        return DRVPLL_ERR_PARAM;
    }

    uint32_t foutHz = drvPLL_CalcOutputHz(cfg);
    if (foutHz == 0u) {
        return DRVPLL_ERR_PARAM;
    }

    /* RM 17.4.1.46 AHBCLKDIV: "Divide by (DIV+1)". */
    uint32_t sysClkHz = foutHz / ((uint32_t)cfg->ahbDiv + 1u);

    /* RM Table 320 (p.1199): system_clk ceiling is 150/100/50 MHz for OD/SD/MD. */
    uint32_t ceilingHz = (cfg->mode == DRVPLL_MODE_OD) ? 150000000u :
                         (cfg->mode == DRVPLL_MODE_SD) ? 100000000u : 50000000u;
    if (sysClkHz > ceilingHz) {
        return DRVPLL_ERR_PARAM;
    }

    /* RM 17.4.1.46 CLKUNLOCK: 1 freezes every xxxDIV / xxxSEL register. Writes
     * would be dropped with no fault, so fail loudly instead. */
    if ((SYSCON0->CLKUNLOCK & SYSCON_CLKUNLOCK_UNLOCK_MASK) != 0u) {
        return DRVPLL_ERR_CLK_FROZEN;
    }

    /* Order matters: headroom first, speed second. */
    drvPLL_Status_t st = set_core_voltage(cfg->mode);
    if (st != DRVPLL_OK) { return st; }

    raise_flash_waitstates(sysClkHz, cfg->mode);

    st = enable_pll_ldo();
    if (st != DRVPLL_OK) { return st; }

    st = enable_source(cfg);
    if (st != DRVPLL_OK) { return st; }

    st = configure_apll(cfg, cfg->srcFreqHz / cfg->ndiv);
    if (st != DRVPLL_OK) { return st; }

    /* RM 34.3.4.7 step 8: the CPU divider "must be complete before connecting
     * the PLL" - otherwise main_clk jumps while the divider still holds the
     * old ratio. */
    SYSCON0->AHBCLKDIV = SYSCON_AHBCLKDIV_DIV((uint32_t)cfg->ahbDiv);
    if (!WAIT_FOR((SYSCON0->AHBCLKDIV & SYSCON_AHBCLKDIV_UNSTAB_MASK) == 0u)) {
        return DRVPLL_ERR_DIVIDER_UNSTAB;
    }

    /* RM 33.2 NOTE: the main-clock multiplexers are synchronized - both the
     * current and the incoming clock must be running for a glitch-free switch.
     * FIRC is deliberately left enabled above for exactly this reason. */
    SCG0->RCCR = SCG_RCCR_SCS(SCS_APLL);

    /* Writing RCCR is a request. CSR is the hardware's answer (RM p.1241
     * step 5: "Read CSR[SCS] until it returns 5"). */
    if (!WAIT_FOR(((SCG0->CSR & SCG_CSR_SCS_MASK) >> SCG_CSR_SCS_SHIFT) == SCS_APLL)) {
        return DRVPLL_ERR_SWITCH_TIMEOUT;
    }

    __DSB();
    __ISB();

    s_coreClockHz   = sysClkHz;
    SystemCoreClock = sysClkHz;
    return DRVPLL_OK;
}

uint32_t drvPLL_GetCoreClockHz(void)
{
    return s_coreClockHz;
}
