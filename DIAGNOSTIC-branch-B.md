# Branch B: Move DRV8452 Bit-Bang to Alternate Pins

**Branch**: `test/move-drv8452-alt-pins`  
**Hypothesis**: The specific pins PB6/PB8 conflict with the display CS/DC lines on the PCB.  
**Parent**: `ruben/find-homing-err` (`93fdb7a2`)  
**Prerequisite**: Branch A must show that disabling bit-bang fixes the crash. If Branch A does NOT fix it, skip this branch.

## What Changed

**One file modified**: `app/src/stepper/drv8452_spi.c`

Pin assignments and GPIO port changed:

| Signal | Old (GPIOB) | New (GPIOC) |
|---|---|---|
| nCS | PB6 | PC0 |
| SCLK | PB7 | PC1 |
| MOSI | PB8 | PC2 |
| MISO | PB5 | PC3 |

The `bb_gpio` device handle changed from `gpiob` to `gpioc`. All SPI protocol logic, timing, and call sites are unchanged.

## Before Flashing

The engineer **must verify** from the PCB schematic:
1. PC0, PC1, PC2, PC3 are routed to accessible pads on the board
2. These pins are not used by any other peripheral
3. If PC0-PC3 are not available, substitute with other verified-free pins and update the defines

The DRV8452 will NOT respond on these new pins (it's wired to J502 bottom-row which connects to the old PB pins). That's expected — this test only checks whether the GPIO activity on the new pins crashes the display. The bit-bang protocol still runs identically.

## What to Observe

Run the standard test procedure from `DIAGNOSTIC.md` Section 9.

- **Display survives** → PB6/PB8 are the conflicting pins. Root cause confirmed.
- **Display crashes** → The pins themselves aren't the issue. Something else about the bit-bang.
