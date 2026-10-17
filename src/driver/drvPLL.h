/*
 * drvPLL - MCXN947 clock tree bring-up (SCG / SPC / FMU / SYSCON)
 *
 * Takes the core from its reset clock (FIRC 48 MHz, RM 34.3.2) up to a
 * caller-chosen frequency by configuring APLL (= PLL0, RM Figure 127).
 *
 * The driver carries no policy: it never mentions a target frequency. The
 * caller supplies source, dividers and the operating mode; the driver only
 * drives registers and reports what the hardware answered.
 *
 * References are to MCX N Reference Manual, Rev. 7, 2025-10-10.
 */
#ifndef DRVPLL_H
#define DRVPLL_H

#include <stdint.h>
#include <stdbool.h>

/* PLL reference source. RM 34.7: APLLCTRL[SOURCE] offers exactly these two. */
typedef enum {
    DRVPLL_SRC_SOSC = 0u,   /* external oscillator ("clk_in")              */
    DRVPLL_SRC_FIRC = 1u,   /* internal 48 MHz FIRC ("fro_hf")             */
} drvPLL_Source_t;

/*
 * Operating mode. RM p.1316: "In OD mode, VDD_CORE is 1.2 V; in SD mode,
 * VDD_CORE is 1.1 V; and in MD mode, VDD_CORE is 1.0 V."
 * The numeric values are SPC ACTIVE_CFG[CORELDO_VDD_LVL] encodings (RM p.1398).
 * Frequency ceilings come from RM Table 320 (p.1199).
 */
typedef enum {
    DRVPLL_MODE_MD = 1u,    /* 1.0 V, system_clk <=  50 MHz */
    DRVPLL_MODE_SD = 2u,    /* 1.1 V, system_clk <= 100 MHz */
    DRVPLL_MODE_OD = 3u,    /* 1.2 V, system_clk <= 150 MHz */
} drvPLL_Mode_t;

/*
 * SOSC frequency range. RM 34.7 SOSCCFG[RANGE].
 * Only meaningful when source == DRVPLL_SRC_SOSC.
 */
typedef enum {
    DRVPLL_SOSC_16_20MHZ = 0u,
    DRVPLL_SOSC_20_30MHZ = 1u,
    DRVPLL_SOSC_30_50MHZ = 2u,
    DRVPLL_SOSC_50_66MHZ = 3u,
} drvPLL_SoscRange_t;

/*
 * PLL divider chain. RM Table 326 (p.1224):
 *     Fref = Fin / N
 *     Fcco = M * Fref
 *     Fout = Fcco / (2 * P)     when bypassPostDiv2 == false
 *     Fout = Fcco / P           when bypassPostDiv2 == true
 *
 * SELR/SELI/SELP are NOT in this struct: RM Equation 1 and 2 (p.1227) define
 * them as a function of M, so the driver derives them. SELR is always 0.
 */
typedef struct {
    drvPLL_Source_t    source;
    uint32_t           srcFreqHz;       /* actual input frequency, Hz        */
    drvPLL_SoscRange_t soscRange;       /* SOSC only                         */
    bool               soscIsCrystal;   /* true = crystal, false = ext clock */

    uint8_t            ndiv;            /* N, 1..255   (RM 34.6 step 5)      */
    uint16_t           mdiv;            /* M, 1..65535                       */
    uint8_t            pdiv;            /* P, 1..31                          */
    bool               bypassPostDiv2;  /* APLLCTRL[BYPASSPOSTDIV2]          */

    uint8_t            ahbDiv;          /* system_clk = main_clk/(ahbDiv+1)  */
    drvPLL_Mode_t      mode;            /* core voltage / speed grade        */
} drvPLL_Config_t;

typedef enum {
    DRVPLL_OK = 0,
    DRVPLL_ERR_PARAM,          /* config violates an RM 34.6 constraint      */
    DRVPLL_ERR_CLK_FROZEN,     /* SYSCON CLKUNLOCK[UNLOCK] = 1               */
    DRVPLL_ERR_REG_LOCKED,     /* SOSCCSR[LK] or APLLCSR[LK] set             */
    DRVPLL_ERR_SPC_TIMEOUT,    /* SPC SC[BUSY] never cleared                 */
    DRVPLL_ERR_LDO_TIMEOUT,    /* SCG LDOCSR[VOUT_OK] never set              */
    DRVPLL_ERR_SRC_TIMEOUT,    /* SOSCVLD / FIRCACC never asserted           */
    DRVPLL_ERR_SRC_ERROR,      /* SOSCCSR[SOSCERR] set                       */
    DRVPLL_ERR_DIV_TIMEOUT,    /* APLLSTAT[xDIVACK] handshake failed         */
    DRVPLL_ERR_LOCK_TIMEOUT,   /* APLLCSR[APLL_LOCK] never set               */
    DRVPLL_ERR_PLL_ERROR,      /* APLLCSR[APLLERR] set                       */
    DRVPLL_ERR_DIVIDER_UNSTAB, /* AHBCLKDIV[UNSTAB] never cleared            */
    DRVPLL_ERR_SWITCH_TIMEOUT, /* CSR[SCS] never matched RCCR[SCS]           */
} drvPLL_Status_t;

/*
 * Bring the clock tree up per cfg. On any error the function returns before
 * touching RCCR[SCS], so the core keeps running on its previous clock.
 * On success, SystemCoreClock holds the new system_clk.
 */
drvPLL_Status_t drvPLL_Init(const drvPLL_Config_t *cfg);

/* Last successfully configured system_clk in Hz (0 before a successful Init). */
uint32_t drvPLL_GetCoreClockHz(void);

/*
 * Compute Fout for a config without touching hardware, per RM Table 326.
 * Returns 0 if the config violates an RM 34.6 constraint. Useful for a
 * compile-time-ish sanity check and for tests.
 */
uint32_t drvPLL_CalcOutputHz(const drvPLL_Config_t *cfg);

#endif /* DRVPLL_H */
