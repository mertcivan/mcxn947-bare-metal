# Third-Party Components

This directory contains unmodified vendor files required to build for the
MCXN947. They are **not** part of the firmware written in this project — the
application, driver, HAL, BSP and scheduler layers under `src/` are original work.

## NXP — MCXN947 device support

| Path | Contents |
|---|---|
| `MCXN947/MCXN947_cm33_core0*.h` | CMSIS device header (register map) |
| `MCXN947/periph/PERI_*.h` | Peripheral register definitions |
| `MCXN947/fsl_device_registers.h` | Umbrella include |
| `MCXN947/system_MCXN947_cm33_core0.c/.h` | `SystemInit()` — FPU, VTOR, ECC, cache |
| `MCXN947/startup_MCXN947_cm33_core0.S` | Vector table and reset handler |
| `MCXN947/MCXN947_cm33_core0_flash.ld` | Linker script (memory map) |

Copyright 1997-2016 Freescale Semiconductor, Inc.
Copyright 2016-2026 NXP
SPDX-License-Identifier: **BSD-3-Clause**

Source: MCUXpresso SDK 26.6.0 for FRDM-MCXN947.

## Arm — CMSIS Core

| Path | Contents |
|---|---|
| `CMSIS/core_cm33.h` | Cortex-M33 core register definitions |
| `CMSIS/cmsis_gcc.h`, `cmsis_compiler.h`, `cmsis_version.h` | Compiler abstraction |
| `CMSIS/m-profile/*.h` | Compiler- and architecture-specific intrinsics |

Copyright (c) 2009-2024 Arm Limited. All rights reserved.
SPDX-License-Identifier: **Apache-2.0**

Full license text: https://www.apache.org/licenses/LICENSE-2.0

---

Each file retains its original copyright and SPDX license header. No vendor file
has been modified.

## Why these files are vendored

Only the register map, startup code and linker script are taken from the SDK.
Every peripheral driver (clock/PLL, GPIO, UART, …) is written from scratch at
register level against the MCX N Reference Manual. Vendoring these four items
keeps the project buildable with nothing but `arm-none-eabi-gcc` and `make` —
no SDK installation, no IDE, no CMake.
