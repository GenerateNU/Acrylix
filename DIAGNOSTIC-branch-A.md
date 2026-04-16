# Branch A: Disable DRV8452 Bit-Bang

**Branch**: `test/disable-drv8452-bitbang`  
**Hypothesis**: DRV8452 bit-bang SPI activity on PB5-PB8 causes the display crash.  
**Parent**: `ruben/find-homing-err` (`93fdb7a2`)

## What Changed

**One file modified**: `app/src/stepper/drv8452_spi.c`

A `#define DRV8452_BITBANG_DISABLED` was added at the top of the file. When defined, all GPIO operations are skipped:

| Function | Normal behavior | With `DRV8452_BITBANG_DISABLED` |
|---|---|---|
| `bb_spi_init()` | Configures PB5-PB8 as GPIO | Prints skip message, no GPIO configured |
| `bb_spi_transfer()` | 16-bit bit-bang frame (~52µs) | Returns 0x0000 immediately |
| `drv8452_spi_init()` | Init + 4 SPI transactions | Calls no-op functions, returns 0 |
| `drv8452_enable()` | Writes EN_OUT=1 via SPI | Prints skip message only |
| `drv8452_disable()` | Writes EN_OUT=0 via SPI | Prints skip message only |

**No other files changed.** All call sites in `main.c`, `states.c` remain identical — they call the same functions, which are now no-ops.

## What to Observe

Run the standard test procedure from `DIAGNOSTIC.md` Section 9.

- **Display survives** → Bit-bang IS the crash cause. Next: run Branch B.
- **Display crashes** → Bit-bang is NOT the cause. Next: run Branch C.

## How to Re-Enable

Comment out or remove the `#define DRV8452_BITBANG_DISABLED` line at `drv8452_spi.c:37`. Clean build.
