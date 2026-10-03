# FRDM-MCXN947 — Bare Metal Firmware

Register-level firmware for the NXP FRDM-MCXN947. No vendor HAL, no `fsl_*`
drivers, no IDE project files, no CMake. Every peripheral driver is written from
scratch against the *MCX N Reference Manual*, using nothing from the SDK but the
register map, the startup file and the linker script.

Build requirements are `arm-none-eabi-gcc` and `make`. Nothing else.

```sh
make          # build
make flash    # program the board over LinkServer
```

## Why

Vendor HALs are convenient and hide the hardware. This project goes the other
way on purpose: clock tree, pin multiplexing, GPIO and serial I/O are brought up
by writing the peripheral registers directly, so the silicon behaviour is
visible rather than abstracted away. It is a learning project first — the
structure is kept production-shaped so it stays useful as a reference.

## Hardware

| | |
|---|---|
| Board | NXP FRDM-MCXN947 |
| MCU | MCXN947VDF — dual-core Arm Cortex-M33, up to 150 MHz |
| Core used | `cm33_core0` |
| Flash | 2 MB dual-bank, base `0x0000_0000` |
| Debug probe | on-board MCU-Link (CMSIS-DAP) |
| Reference | MCX N Reference Manual, Rev. 7 · Board UM12018 |

Reset state: FIRC is enabled out of reset with `FIRCCFG[RANGE] = 0`, so the part
starts on the **48 MHz** FIRC clock (RM §34.5).

## Written here vs. vendored

Everything under `src/` is original. Everything under `vendor/` is unmodified
NXP / Arm material, kept only so the project builds without an SDK install.

| Written from scratch | Vendored (see [`vendor/NOTICE.md`](vendor/NOTICE.md)) |
|---|---|
| Peripheral drivers (`src/driver/`) | Register map — `MCXN947_*.h`, `PERI_*.h` |
| Hardware abstraction (`src/hal/`) | `core_cm33.h` and CMSIS compiler headers |
| Board support (`src/bsp/`) | `startup_MCXN947_cm33_core0.S` — vector table |
| Scheduler (`src/scheduler/`) | `MCXN947_cm33_core0_flash.ld` — linker script |
| Application (`src/application/`) | `system_MCXN947_cm33_core0.c` — `SystemInit()` |

The register map is a machine-generated description of the silicon, not driver
code — reimplementing it by hand would add risk without adding understanding.

## Architecture

Layers depend downward only; no layer reaches past its neighbour.

```
  application/   what the product does
  scheduler/     when it runs
  bsp/           this board: which pin is which LED
  hal/           chip-independent interface (gpio_write, uart_send)
  driver/        registers: PLL, GPIO, UART, timers
  ────────────────────────────────────────────────
  hardware
```

## Requirements

| Tool | Version used | Notes |
|---|---|---|
| Arm GNU Toolchain | 14.2.rel1 | `arm-none-eabi-gcc` must be on `PATH` |
| GNU make | 4.4.1 | |
| NXP LinkServer | 26.6.137 | only for `make flash` / debugging |

If the toolchain is not on `PATH`:

```sh
make ARMGCC_DIR=/c/path/to/arm-gnu-toolchain
```

## Building

```sh
make            # build -> build/mcxn947-bare-metal.{elf,bin,hex}
make size       # per-section memory usage
make flash      # program via LinkServer
make erase      # mass erase
make clean      # remove build output
```

With more than one probe attached, select one:

```sh
make flash PROBE=<serial>
```

After `make flash` the target halts at reset — press **SW1 (Reset)** to run.

## Layout

```
├── Makefile
├── src/
│   ├── main.c
│   ├── application/
│   ├── scheduler/
│   ├── bsp/
│   ├── hal/
│   └── driver/
│       ├── drvPLL.c        clock / PLL bring-up
│       └── drvPLL.h
├── vendor/                 NXP + Arm files, unmodified
│   ├── CMSIS/
│   ├── MCXN947/
│   └── NOTICE.md           licenses and provenance
└── svd/                    register descriptions for the debugger
```

`svd/MCXN947cm33_core0.xml` lets the debugger show peripheral registers by name
and decode their bit fields — worth loading when working at this level.

## Board specifics

RGB LED, active-low (driving the pin low turns the LED **on**):

| Colour | Port | GPIO | Pin | Package | Silkscreen |
|---|---|---|---|---|---|
| Red | PORT0 | GPIO0 | 10 | B12 | `P0_10/SJ5[1]` |
| Green | PORT0 | GPIO0 | 27 | E10 | `P0_27/SJ6[1]` |
| Blue | PORT1 | GPIO1 | 2 | C4 | `P1_2/SJ4[1]` |

Clock gates in `SYSCON->AHBCLKCTRL0`: PORT0 bit 13, PORT1 bit 14, GPIO0 bit 19,
GPIO1 bit 20. A peripheral whose gate is closed silently ignores register writes
— no fault, no warning.

## Status

Early. The toolchain, build system and skeleton are in place and verified; the
drivers are being written one at a time.

- [x] Standalone build system (make, no SDK/CMake dependency)
- [x] Layered project skeleton
- [ ] `drvPLL` — FIRC 48 MHz → PLL0 150 MHz, with flash wait states and core voltage
- [ ] GPIO driver
- [ ] UART driver (Flexcomm4)
- [ ] SysTick-based timing
- [ ] Cooperative scheduler

## License

Code under `src/` — see `LICENSE`.
Third-party material under `vendor/` keeps its original license: NXP files are
BSD-3-Clause, Arm CMSIS is Apache-2.0. Details in [`vendor/NOTICE.md`](vendor/NOTICE.md).
