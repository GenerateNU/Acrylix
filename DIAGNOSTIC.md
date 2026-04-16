# Display Crash Diagnostic Investigation

## 1. Problem Statement

**Symptom**: The ILI9341 TFT display freezes or corrupts during motor-related operations. The MCU continues running — serial/RTT output is normal, no fault dump appears. The display simply stops updating or shows garbage.

**Timeline**:

| Commit | Date | What changed | Display status |
|---|---|---|---|
| `c62d1bb8` | — | Stepper init added to main, buttons debugged | Working |
| `cf8c165a` | Apr 14 18:46 | Motor + UI coordination, progress struct added | Working |
| `4c1f1a50` | Apr 15 14:04 | `display_screen_ready` semaphore, run_countdown fix | Working |
| `02425fea` | Apr 15 15:37 | **`drv8452_enable()` added to BEND flow** | **Crash appears** |
| `93fdb7a2` | Apr 15 20:11 | LVGL defensive fixes, HOMING case added, stack bumped | **Still crashes** |

**What was tried in `93fdb7a2`** (none resolved the crash):
- `lv_task_handler()` flush before `lv_obj_clean()` in `clear_screen()`
- `lv_obj_remove_style_all()` to prevent style accumulation
- NULL checks on every `lv_label_create()` return value
- `printk` breadcrumbs throughout `direction_screen()` to trace crash location
- Display stack increased from 12KB to 20KB
- Stepper calls in HOMING commented out
- `drv8452_enable()` in `homing_entry()` commented out

**Current state** (branch `ruben/find-homing-err` HEAD):
- `drv8452_enable()` in `bend_entry()` (`states.c:138`) — **still active**
- `drv8452_enable()` in `homing_entry()` (`states.c:115`) — commented out
- All `stepper_move_degrees()` calls in HOMING case (`main.c:112,120,137`) — commented out
- `drv8452_disable()` at boot (`main.c:302`) — commented out

---

## 2. Architecture Overview

### Threading Model

```
┌─────────────────────────────────────────────────────────────┐
│  main()                                                      │
│    drv8452_spi_init()  ← bit-bang SPI on PB5-PB8            │
│    stepper_init()      ← PWM on PB0, DIR on PB1             │
│    sm_init()           ← state = IDLE                        │
│    creates state_thread (priority 7, 8KB stack)              │
│    creates display_thread (priority 6, 20KB stack)           │
│    k_sleep(K_FOREVER)                                        │
└─────────────────────────────────────────────────────────────┘

┌──────────────────────────┐    ┌──────────────────────────────┐
│  State Thread (pri 7)     │    │  Display Thread (pri 6)       │
│                           │    │  (higher priority)            │
│  while(1) {               │    │                               │
│    switch(g_sm.current) { │    │  display_init()               │
│      case IDLE: ...       │    │  while(1) {                   │
│      case HOMING: ...     │    │    drain display_queue         │
│      case BEND: ...       │    │    → display_set_state()      │
│    }                      │    │    display_update()            │
│    k_sleep(10ms)          │    │    → button_pressed()         │
│  }                        │    │    → check g_progress flags   │
│                           │    │    → lv_task_handler()  ← SPI1│
│  Calls:                   │    │    k_sleep(10ms)              │
│  - sm_transition()        │    │  }                            │
│  - drv8452_enable()  ← BB│    │                               │
│  - stepper_move_degrees() │    │  Also: encoder ISR, btn ISRs │
└──────────────────────────┘    └──────────────────────────────┘

Communication:
  event_queue   (display → state)  : button presses post events
  display_queue (state → display)  : state transitions post screen changes
  g_progress    (state → display)  : countdown progress via volatile + __DMB
  g_inputs      (display → state)  : user input selections (no synchronization)
```

### Display Hardware Path

The ILI9341 display is driven via **SPI1 hardware** by the Zephyr MIPI DBI driver, triggered by `lv_task_handler()` every 10ms in the display thread.

**Pins (per device tree overlay)**:
- SPI1 SCK: PA5
- SPI1 MISO: PA6
- SPI1 MOSI: PA7
- SPI1 CS: PA4
- MIPI DBI DC: PC6
- MIPI DBI RST: PC7

### DRV8452 Bit-Bang SPI Path

The DRV8452 motor driver is controlled via **GPIO bit-bang SPI** from the state thread. The original SPI2 hardware (PB12-PB15) is unusable due to dead PCB traces on J502 top row.

**Pins (per `drv8452_spi.c`)**:
- nCS: PB6
- SCLK: PB7
- MOSI: PB8
- MISO: PB5

`bb_spi_transfer()` runs for ~52µs per 16-bit frame, toggling these GPIO pins from the state thread. It is called by:
- `drv8452_spi_init()` — 4 transactions at boot
- `drv8452_enable()` — 1 transaction (called from `bend_entry()`)
- `drv8452_disable()` — 1 transaction (called from `complete_entry()`, `error_entry()`)

**There is no synchronization between the bit-bang SPI and the display SPI.**

---

## 3. The Pin Conflict Contradiction

There are two conflicting sources of truth about whether the DRV8452 bit-bang pins interfere with the display:

### Source 1: Device Tree Overlay (says NO conflict)

`app/app.overlay` lines 128 and 132:
```
cs-gpios = <&gpioa 4 GPIO_ACTIVE_LOW>;      → Display CS is PA4
dc-gpios = <&gpioc 6 GPIO_ACTIVE_HIGH>;     → Display DC is PC6
```

PA4 and PB6 are **different physical pins** on the STM32F446RET6 — different GPIO ports, different pads on the LQFP64 package. Same for PC6 and PB8. On the chip, they are electrically independent. If the overlay is correct, toggling PB6/PB8 cannot affect PA4/PC6, and the bit-bang cannot corrupt the display.

### Source 2: Code Comments (says YES conflict)

`drv8452_spi.c` lines 10-14:
```
PB8 = MOSI  *** overlaps MIPI DBI dc-gpios — display broken ***
WARNING: PB6 also serves as SPI1 cs-gpios for the ILI9341 display
```

`app/app.overlay` lines 159-161:
```
NOTE: PB6 overlaps SPI1 cs-gpios (display) and PB8 overlaps MIPI DBI dc-gpios.
Display will not work while DRV8452 bit-bang is active on these pins.
```

### What "Overlap" Means

PA4 and PB6 are independent on the chip. For them to "overlap," there must be a connection **outside the chip** — on the PCB. This means either:

1. **The PCB routes both pins to the same trace** — e.g., the display CS trace connects to both PA4 and PB6 on the STM32. Both pins drive the same wire. When `bb_spi_transfer()` asserts PB6 LOW, it fights with the SPI1 driver controlling PA4.

2. **The overlay is wrong** — the display is actually wired to PB6/PB8 on this board, not PA4/PC6. The Zephyr SPI driver drives PA4/PC6 (connected to nothing), while the display's actual CS/DC are on PB6/PB8 (controlled by the bit-bang code).

3. **The comments are stale** — they were written for a previous prototype (e.g., Nucleo + breadboard) where the wiring was different. On the current PCB, the overlay is correct and there is no conflict.

### Why This Matters

- If Source 1 is correct → the bit-bang is electrically isolated from the display → `bb_spi_transfer()` cannot be the crash cause → we must look elsewhere.
- If Source 2 is correct → every `drv8452_enable()`/`drv8452_disable()` call corrupts the display bus → this IS the crash cause.

**We cannot determine which source is correct from the code alone. The hardware must be checked.**

---

## 4. Hardware Verification (Do This First)

Before flashing any test branches, verify the physical wiring on the PCB with a multimeter in continuity mode.

### Checks

| # | From (STM32 pin) | To (display module pad) | Expected | If unexpected |
|---|---|---|---|---|
| 1 | PA4 (LQFP64 pin 20) | Display CS input | Connected | Overlay might be wrong |
| 2 | PC6 (LQFP64 pin 37) | Display DC input | Connected | Overlay might be wrong |
| 3 | PB6 (LQFP64 pin 42) | Display CS input | **NOT** connected | If connected → Source 2 confirmed |
| 4 | PB8 (LQFP64 pin 45) | Display DC input | **NOT** connected | If connected → Source 2 confirmed |
| 5 | PB6 (LQFP64 pin 42) | J502 bottom-row nCS | Connected | Expected — DRV8452 bit-bang |
| 6 | PB8 (LQFP64 pin 45) | J502 bottom-row MOSI | Connected | Expected — DRV8452 bit-bang |

### Interpreting Results

- **Checks 1-2 pass, checks 3-4 pass** (PA4→CS, PC6→DC, PB6/PB8 NOT on display): Source 1 confirmed. The overlay is correct. Pin conflict is eliminated. Skip Branch B. Focus testing on Branches A and C.

- **Check 3 or 4 fails** (PB6 or PB8 IS connected to display): Source 2 confirmed. The PCB routes DRV8452 bit-bang pins to the display. This is the root cause. Branch B will verify the fix.

- **Checks 1-2 fail** (PA4/PC6 NOT connected to display): The overlay is wrong for this board. The display wiring needs to be traced and the overlay corrected.

---

## 5. Hypothesis A: DRV8452 Bit-Bang Activity Crashes the Display

**Branch**: `test/disable-drv8452-bitbang`

### Reasoning

The crash appeared in commit `02425fea` exactly when `drv8452_enable()` was added to the BEND state flow. Every LVGL software fix attempted in commit `93fdb7a2` failed to resolve it. The only effective workaround was commenting out the `drv8452_enable()` call. This pattern — software fixes don't help, disabling the hardware call does — points to the bit-bang GPIO activity itself as the trigger.

### Evidence For
- Crash timeline correlates precisely with `drv8452_enable()` introduction
- LVGL defensive fixes (flush, NULL checks, stack increase) had no effect
- Commenting out `drv8452_enable()` was the only thing that worked

### Evidence Against
- If overlay pins are correct (PA4/PC6), bit-bang on PB6/PB8 is on a different GPIO port entirely — electrically isolated

### What This Branch Changes

**File: `app/src/stepper/drv8452_spi.c`**

All GPIO operations are wrapped in `#ifndef DRV8452_BITBANG_DISABLED`. A `#define DRV8452_BITBANG_DISABLED` at the top of the file activates the diagnostic mode. When disabled:

- `bb_spi_init()` → skips all `gpio_pin_configure()` calls, prints `"DIAG: bb_spi_init SKIPPED"`
- `bb_spi_transfer()` → returns `0x0000` immediately, prints `"DIAG: bb_spi_transfer SKIPPED"`
- `drv8452_spi_init()` → calls the no-op `bb_spi_init` and `bb_spi_transfer`, returns 0
- `drv8452_enable()` → prints `"DIAG: drv8452_enable SKIPPED"`, no GPIO
- `drv8452_disable()` → prints `"DIAG: drv8452_disable SKIPPED"`, no GPIO

**No other files are changed.** The call sites in `main.c` (`drv8452_spi_init()`), `states.c` (`drv8452_enable()` in `bend_entry()`, `drv8452_disable()` in `complete_entry()`/`error_entry()`) remain in place — they call the same functions, which now do nothing.

### Flow With Changes

```
Boot:
  drv8452_spi_init() → prints "SKIPPED", no GPIO configured
  stepper_init()     → PWM and DIR configured normally
  
IDLE → user presses forward → INITIALIZATION → user selects inputs → BEND:
  sm_transition(STATE_BEND):
    posts to display_queue
    bend_entry():
      drv8452_enable() → prints "SKIPPED", no GPIO toggled    ← CHANGE
      limit switch check (unchanged)
  
  STATE_BEND case:
    k_sleep(200ms)
    stepper_move_degrees(target, 10 RPM) → PWM runs, motor steps
      (H-bridge is uncontrolled — motor may not move, but PWM + ISR still fire)
    sm_transition(STATE_COOL)
  
  Display thread runs normally throughout — lv_task_handler() renders every 10ms
  NO bit-bang GPIO activity occurs AT ANY POINT
```

### How to Interpret Results

- **Display survives the full flow** → The bit-bang activity IS the cause of the crash. Proceed to Branch B to determine whether it's the specific pins (PB6/PB8) or something else about the GPIO operations.

- **Display still crashes** → The bit-bang is NOT the cause. The DRV8452 code is exonerated. Proceed to Branch C to test the LVGL reentrancy hypothesis.

### How to Re-Enable

Remove or comment out `#define DRV8452_BITBANG_DISABLED` at the top of `drv8452_spi.c`. Clean build.

---

## 6. Hypothesis B: PB6/PB8 Specifically Conflict With Display Pins

**Branch**: `test/move-drv8452-alt-pins`

### Reasoning

If Branch A shows that disabling bit-bang fixes the crash, the next question is: is it the **specific pins** (PB6/PB8) that are the problem, or is it something else about the GPIO bit-bang activity (timing, driver interaction, interrupt priority)?

Moving the bit-bang to completely different GPIO pins — while keeping the protocol, timing, and call pattern identical — isolates the pin variable.

### Prerequisite

**Run Branch A first.** If Branch A does NOT fix the crash, skip this branch entirely — the pins are irrelevant if the bit-bang itself isn't the cause.

### Evidence For
- Code comments explicitly name PB6 and PB8 as overlapping display signals
- Both `drv8452_spi.c` and `app.overlay` document this independently

### Evidence Against
- Overlay says CS=PA4 and DC=PC6, which are on different GPIO ports than PB6/PB8

### What This Branch Changes

**File: `app/src/stepper/drv8452_spi.c`**

Pin defines and GPIO port changed to alternate pins. **The engineer must verify these alternate pins are accessible on the PCB before flashing.** The candidates below assume PC0-PC3 are free; substitute with verified-free pins if needed.

```
Old (current):              New (this branch):
  PB5 = MISO (GPIOB)         PC3 = MISO (GPIOC)
  PB6 = nCS  (GPIOB)         PC0 = nCS  (GPIOC)
  PB7 = SCLK (GPIOB)         PC1 = SCLK (GPIOC)
  PB8 = MOSI (GPIOB)         PC2 = MOSI (GPIOC)
```

Changes:
- `SPI_NCS_PIN`, `SPI_SCLK_PIN`, `SPI_MOSI_PIN`, `SPI_MISO_PIN` defines updated
- `bb_gpio` device handle changed from `DT_NODELABEL(gpiob)` to `DT_NODELABEL(gpioc)`
- All `gpio_pin_set(bb_gpio, ...)` and `gpio_pin_get(bb_gpio, ...)` calls use the new pin numbers automatically (they reference the defines)

**No other files are changed.**

### Important: Hardware Wiring Required

The DRV8452 on J502 bottom-row still expects signals on its original pads. For this test to actually communicate with the DRV8452, the engineer would need to bodge-wire PC0-PC3 to the J502 bottom-row pads. **However, for the purpose of this diagnostic, the DRV8452 does not need to respond.** The test is whether the GPIO toggling activity on the new pins crashes the display. The bit-bang code will still run its full protocol (assert CS, clock data, deassert CS) — it will just be on pins that are NOT connected to the display.

### Flow With Changes

Identical to normal operation. `bb_spi_transfer()` runs at the same points (boot, `bend_entry`, `complete_entry`/`error_entry`), with the same timing (~52µs per frame), toggling the same number of GPIO edges — just on PC0-PC3 instead of PB5-PB8.

### How to Interpret Results

- **Display survives** → PB6/PB8 ARE the conflicting pins. The PCB routes them to the display. **Root cause confirmed.** Fix: permanently move the DRV8452 bit-bang to the alternate pins (with proper bodge wiring to J502), or fix the PCB routing in the next board revision.

- **Display still crashes** → The specific pins are NOT the issue. Something else about the bit-bang activity (e.g., GPIO driver locking, interrupt masking during `k_busy_wait`, power rail noise from GPIO switching) causes the crash. Needs deeper investigation with a logic analyzer.

---

## 7. Hypothesis C: LVGL Reentrancy From Encoder ISR

**Branch**: `test/fix-lvgl-isr-reentrancy`

### Reasoning

The encoder ISR (`enc_isr` in `display.c:186-222`) directly calls LVGL rendering functions from interrupt context. LVGL is not designed for concurrent access — it has no internal locking. If the ISR fires while `lv_task_handler()` is mid-execution in the display thread, LVGL's internal state (draw buffers, object tree, style cache) can be corrupted. This corruption may not manifest immediately — it could cause a crash on a later screen transition or render pass.

### Evidence For
- `enc_isr()` calls `bend_anim_set_angle()` which calls:
  - `lv_line_set_points()` — 4 times (modifies line object coordinates)
  - `lv_label_set_text()` — 1 time (modifies label text buffer)
- These run in **ISR context** while `lv_task_handler()` runs in the display thread
- LVGL documentation explicitly states it is not thread-safe
- The codebase already uses the deferred-flag pattern for `thick_update_pending` — the encoder angle path was never converted to match

### Evidence Against
- The ISR only calls LVGL when `anim_fixed_bot != NULL` — this is only true on the bend angle input screen (INPUT_STEP_BEND_ANGLE)
- During HOMING and BEND states, the screen shows directions or process — `anim_fixed_bot` is NULL, so the ISR returns early
- However, corruption during input selection could persist in LVGL's internal heap and manifest later when transitioning to BEND

### What This Branch Changes

**File: `app/src/display/display.c`**

Two new static variables added near the existing `thick_update_pending`:
```c
static volatile bool enc_angle_update_pending = false;
static volatile int  enc_pending_angle = 0;
```

**In `enc_isr()`** — the bend angle branch (lines 205-211) changes from:
```c
/* OLD — LVGL called from ISR */
if (anim_fixed_bot != NULL) {
    sel_bend_angle += delta;
    if (sel_bend_angle < 0)  sel_bend_angle = 0;
    if (sel_bend_angle > 90) sel_bend_angle = 90;
    bend_anim_set_angle(sel_bend_angle);    ← LVGL calls here
    printk("Bend angle: %d deg\n", sel_bend_angle);
    return;
}
```
to:
```c
/* NEW — flag only, LVGL deferred to display thread */
if (anim_fixed_bot != NULL) {
    sel_bend_angle += delta;
    if (sel_bend_angle < 0)  sel_bend_angle = 0;
    if (sel_bend_angle > 90) sel_bend_angle = 90;
    enc_pending_angle = sel_bend_angle;
    enc_angle_update_pending = true;         ← flag set, no LVGL
    printk("Bend angle: %d deg\n", sel_bend_angle);
    return;
}
```

**In `display_update()`** — new handler block added before `lv_task_handler()`:
```c
/* Handle deferred encoder angle updates */
if (enc_angle_update_pending) {
    enc_angle_update_pending = false;
    bend_anim_set_angle(enc_pending_angle);
}
```

This follows the exact same pattern as the existing `thick_update_pending` handler at `display.c:988-991`.

**No other files are changed.** DRV8452 bit-bang remains active on original PB5-PB8 pins.

### Flow With Changes

```
User rotates encoder on bend angle screen:
  OLD: enc_isr → bend_anim_set_angle → lv_line_set_points (ISR context!)
  NEW: enc_isr → sets flag + angle value → returns immediately

Next display_update() call (within 10ms):
  display_update():
    button_pressed()
    check thick_update_pending
    check g_progress.pending
    check enc_angle_update_pending → true → bend_anim_set_angle()  ← THREAD context
    lv_task_handler()
```

The visual behavior is identical — the angle animation updates within 10ms of the encoder turn. But LVGL is only ever touched from the display thread.

### How to Interpret Results

- **Display survives** → LVGL reentrancy was the cause, or a contributing factor. **This fix should be kept permanently regardless of other test results** — calling LVGL from ISR context is always wrong.

- **Display still crashes** → LVGL ISR calls were not the crash cause for this specific symptom. The fix is still correct and should be kept, but the root cause is elsewhere. Focus on Branch A results.

---

## 8. Decision Tree

```
                    ┌─────────────────────────────┐
                    │  Hardware checks (Section 4)  │
                    │  Multimeter on PCB            │
                    └──────────┬──────────────────┘
                               │
                    ┌──────────▼──────────────────┐
                    │  PB6→display CS or            │
                    │  PB8→display DC?              │
                    └──────┬─────────────┬────────┘
                       YES │             │ NO
                           │             │
              ┌────────────▼──┐   ┌──────▼─────────────────┐
              │ Source 2       │   │ Source 1 confirmed.     │
              │ confirmed.     │   │ Pin conflict eliminated.│
              │ Pin conflict   │   │                         │
              │ is root cause. │   │ Flash Branch A          │
              │                │   │ (disable bit-bang)      │
              │ Flash Branch B │   └──────┬────────────┬────┘
              │ to verify fix  │      crashes│         │survives
              └────────────────┘          │         │
                                   ┌──────▼───┐ ┌──▼──────────────┐
                                   │Flash     │ │Bit-bang IS the   │
                                   │Branch C  │ │cause, but pins   │
                                   │(LVGL fix)│ │don't overlap     │
                                   └──┬────┬──┘ │per multimeter.   │
                                 crash│    │surv│                  │
                                      │    │    │Flash Branch B    │
                               ┌──────▼┐ ┌▼──┐ │(alt pins) to     │
                               │Unknown│ │ISR │ │investigate       │
                               │cause  │ │was │ │further.          │
                               │deeper │ │the │ └──────────────────┘
                               │invest.│ │fix │
                               └───────┘ └────┘
```

---

## 9. How to Switch Between Branches

### Branch Names

| Branch | Base | Purpose |
|---|---|---|
| `ruben/find-homing-err` | `main` | Current investigation branch (this doc lives here) |
| `test/disable-drv8452-bitbang` | `ruben/find-homing-err` | Hypothesis A: disable all bit-bang |
| `test/move-drv8452-alt-pins` | `ruben/find-homing-err` | Hypothesis B: move to non-conflicting pins |
| `test/fix-lvgl-isr-reentrancy` | `ruben/find-homing-err` | Hypothesis C: defer LVGL from encoder ISR |

### Switching Procedure

```bash
# 1. Check out the branch
git checkout test/disable-drv8452-bitbang

# 2. ALWAYS clean build when switching branches
west build -p always -b acrylix app

# 3. Flash
west flash

# 4. Run the test procedure (same every time — see below)

# 5. Record RTT output

# 6. Switch to next branch and repeat
git checkout test/move-drv8452-alt-pins
west build -p always -b acrylix app
west flash
```

### Test Procedure (Identical for Every Branch)

1. Power on the board. Wait for the directions screen to appear.
2. Press FORWARD. Verify the bend radius screen (1/3) appears.
3. Press FORWARD. On the bend angle screen (2/3), rotate the encoder to select ~45 degrees.
4. Press FORWARD. On the thickness screen (3/3), select 1/16".
5. Press FORWARD to confirm inputs. The process screen ("Heating...") should appear.
6. **Observe the display through the BEND phase** — this is where `drv8452_enable()` fires and `stepper_move_degrees()` runs.
7. Observe through COOL and COMPLETE.
8. **Record**: did the display survive? Did it freeze, corrupt, or go blank? At what point?
9. Save the RTT serial log for comparison.

### Results Table

Fill in after testing each branch:

| Branch | Display survived? | Crash point (if any) | RTT log notes |
|---|---|---|---|
| `ruben/find-homing-err` (baseline) | | | |
| `test/disable-drv8452-bitbang` | | | |
| `test/move-drv8452-alt-pins` | | | |
| `test/fix-lvgl-isr-reentrancy` | | | |
