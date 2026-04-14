# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

AcrylicBender is a Zephyr RTOS firmware project for an STM32F446RET6-based custom PCB. It controls a semi-automated desktop acrylic bending machine with a TFT touchscreen display, rotary encoder input, dual NEMA 23 stepper motors, AC heating via SSR, and PT1000 temperature sensing.

## Board Targets

Two build targets are used during development:

| Target | Board String | Use |
|---|---|---|
| NUCLEO-F446RE | `nucleo_f446re` | Primary prototyping and firmware development |
| AcrylicBender custom PCB | `acrylix` (TBD) | Final hardware target |

> **Important:** Code is actively developed and tested on the NUCLEO-F446RE. The custom PCB shares the same STM32F446RET6 MCU but has a different pinout and peripheral wiring. Always verify pin assignments against the custom board schematic before flashing to the custom PCB. A successful flash does **not** guarantee correct behavior if the build target does not match the physical board.

## Build Commands

This project uses West (Zephyr's workspace manager) and CMake, built within VS Code.

```bash
# Initial setup (run once)
west init -l .
west update

# Build for NUCLEO (prototyping)
west build -b nucleo_f446re app

# Build for custom PCB (once board definition is finalized)
west build -b acrylix app

# Clean build (use when switching board targets)
west build -p always -b nucleo_f446re app

# Flash via ST-LINK/V2
west flash
```

> Always do a clean build (`-p always`) when switching between `nucleo_f446re` and the custom board target to avoid stale device tree or Kconfig artifacts.

## Hardware — AcrylicBender Custom PCB

- **MCU:** STM32F446RET6 (LQFP64), 180 MHz, Cortex-M4F
- **Debug/Flash:** SWD via ST-LINK/V2 (SWDIO/SWDCLK + nRST); supports breakpoints, variable watching, and optional SWO trace
- **Display:** Adafruit 2.8"/3.2" ILI9341 TFT touchscreen over SPI1 (CS=PA4, CLK=PA5, MISO=PA6, MOSI=PA7, DC=PC6, RST=PC7)
- **User input:** PEC11R incremental rotary encoder (UI dial) — ENC_A=PC10, ENC_B=PC11; FORWARD_BTN=PA8 (active low), BACK_BTN=PA9 (active low)
- **Stepper motors:** 1x NEMA 23 (OMC 23HS22-4004-ME1K, 4A/phase) driven by DRV8452 in hardware interface mode (no SPI); current set via VREF resistor divider (R1=10kΩ, R2=56kΩ, ~3.2A peak)
- **Heating:** AC heater switched via SSR-25DA solid state relay; PWM control on PA8 (TIM1_CH1)
- **Temperature sensing:** PT1000 RTD (NB-PTCO-006) via voltage divider → PC0 (ADC1_IN6)
- **Motor position encoders (not UI):** PA0=ENC_A_OUT, PA1=ENC_B_OUT, PA2=ENC_Z_OUT — differential quadrature via AM26C32 line receivers → TIM4 (PB6/PB7) and TIM8 (PC6/PC7); do not use these pins for UI input
- **Bend angle feedback:** AS5600 absolute magnetic encoder on I2C → PB10/PB11
- **Power:** 24V input → 5.5V buck → 5V LDO → 3.3V LDO

## Software Components

**`app/src/display/`** — Display and encoder subsystem. Owns the LVGL UI, handles PEC11R rotary encoder input, and exposes `display_init()` / `display_update()` to main.

**`app/src/states/`** — State machine: `IDLE → HOMING → INITIALIZATION → BEND → COOL → COMPLETE` (plus `ERROR`). The `sm_context_t` struct tracks completion flags and error codes. State transitions go through `states_transition()`.

**`app/src/stepper/`** — Stepper motor control for DRV8452 drivers (hardware interface mode). Step/direction GPIO signals; no SPI communication with driver.

**`app/src/temp/`** — PT1000 temperature sensing via ADC. Resistance calculated from voltage divider readback; no dedicated converter IC.

**`app/src/heating/`** — SSR PWM control for AC heater via TIM1_CH1 on PA8.

**`app/src/board_logic/`** — Orchestration layer connecting state machine to hardware subsystems.

**`app/src/main.c`** — Entry point: initializes all subsystems, runs main loop.

## Configuration

- **`app/prj.conf`** — Zephyr Kconfig: enables GPIO, SPI (display), I2C (AS5600), ADC (PT1000), PWM/TIM (SSR), UART console, LVGL.
- **`app/app.overlay`** — Device tree overlay: maps all peripherals to STM32F446RET6 pins per the custom PCB schematic.
- **`west.yml`** — Pulls Zephyr and LVGL as workspace dependencies.

## Flashing & Debugging Notes

- Flash and debug via **SWD using ST-LINK/V2** and `west flash` / STM32CubeIDE / STM32CubeProgrammer.
- The NUCLEO-F446RE has an onboard ST-LINK; the custom PCB requires an external ST-LINK/V2.
- **A "flash successful" message only means the binary was written — it does not confirm the firmware is running correctly.** If GPIO or peripherals are unresponsive on the custom PCB after a successful flash, verify:
  1. The correct board target was built (not `nucleo_f446re` for custom PCB)
  2. Pin assignments in `app.overlay` match the custom PCB schematic
  3. Clock and power domains are active for the relevant peripheral
  4. The ST-LINK nRST line is connected and the MCU is properly reset after flashing
- Use **SWO trace or UART logging** to confirm code execution reaches expected entry points before debugging peripheral behavior.