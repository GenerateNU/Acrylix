# Race Condition Fix: LVGL Reentrancy From Encoder ISR

## The Problem

The encoder ISR (`enc_isr` in `display.c:186`) directly calls LVGL rendering functions from interrupt context. LVGL is not thread-safe or ISR-safe — it has no internal locking. The display thread calls `lv_task_handler()` every 10ms to render the UI. If the encoder ISR fires mid-render, two execution contexts modify LVGL's internal state simultaneously, corrupting its heap, object tree, or draw queue.

### Where It Happens

In `app/src/display/display.c`, the encoder ISR handles the bend angle input screen (step 2 of 3):

```c
// enc_isr() — runs in INTERRUPT context
if (anim_fixed_bot != NULL) {
    sel_bend_angle += delta;
    bend_anim_set_angle(sel_bend_angle);   // ← calls LVGL from ISR
}
```

`bend_anim_set_angle()` calls:
- `lv_line_set_points()` — 4 times (modifies line object coordinates)
- `lv_label_set_text()` — 1 time (allocates and copies a string in LVGL's heap)

These fire inside the ISR while `lv_task_handler()` on the display thread is potentially mid-execution — traversing the object tree, processing dirty flags, flushing draw buffers. The two contexts modify the same data structures with no synchronization.

### The Existing Pattern

The thickness screen (step 3 of 3) in the same ISR already handles this correctly:

```c
// enc_isr() — thickness branch
if (thick_bar_line != NULL) {
    sel_thickness ^= 1;
    thick_update_pending = true;   // ← flag only, no LVGL calls
}
```

The display thread picks up the flag in `display_update()`:

```c
if (thick_update_pending) {
    thick_update_pending = false;
    thick_anim_set(sel_thickness);  // ← LVGL calls in thread context
}
```

The bend angle path was never converted to match this pattern.

## How It Causes the Display Crash

The corruption is latent. It doesn't crash the display immediately on the bend angle screen. Instead, it corrupts a pointer or linked list node inside LVGL's 32KB memory pool. The crash manifests later when LVGL exercises that corrupted structure — typically during the screen transitions into HOMING or BEND.

### Flow: Input Selection → HOMING → Crash

```
IDLE
  └→ user presses forward
INITIALIZATION
  └→ bend radius screen (step 1/3) — no encoder LVGL calls
  └→ bend angle screen (step 2/3)
        User rotates encoder.
        enc_isr fires repeatedly.
        Each tick: bend_anim_set_angle() runs in ISR context     ← CORRUPTION HERE
        while lv_task_handler() runs on display thread.
        LVGL internal heap/object tree corrupted.
  └→ thickness screen (step 3/3) — uses deferred flag, safe
  └→ user confirms inputs

sm_transition(STATE_HOMING)
  └→ posts STATE_HOMING to display_queue
  └→ homing_entry() runs on state thread

Display thread processes STATE_HOMING:
  └→ display_set_state(STATE_HOMING)
  └→ no handler (default: break) — screen not changed, old objects still live

HOMING state runs on state thread:
  └→ motor moves (if uncommented), or tight loop
  └→ sm_transition(STATE_INITIALIZATION)

Display thread processes STATE_INITIALIZATION:
  └→ input_selection_enter()
  └→ bend_radius_screen()
  └→ clear_screen()
  └→ lv_obj_clean(lv_scr_act())                                 ← CRASH HERE
        Traverses object tree to delete children.
        Follows corrupted linked list pointer.
        Display freezes or renders garbage.
```

The same flow applies if BEND comes after input selection instead of HOMING — `process_screen()` calls `clear_screen()` which hits the corrupted LVGL state.

### Why the Serial Monitor Shows Nothing

The corruption is entirely within LVGL's 32KB memory pool (`CONFIG_LV_Z_MEM_POOL_SIZE=32768`). Corrupted pointers stay within valid RAM — LVGL's own heap region. There is no invalid memory access that would trigger a HardFault or MPU violation. The state thread continues running, `printk` continues writing to RTT, the MCU is fine. Only the display thread is stuck (infinite loop in a corrupted list traversal, or rendering garbage coordinates).

## The Fix

Three changes in `app/src/display/display.c`. No other files modified.

### 1. New deferred flag variables

```c
static volatile bool enc_angle_update_pending = false;
static volatile int  enc_pending_angle        = 0;
```

### 2. ISR: set flag instead of calling LVGL

```c
// enc_isr() — bend angle branch
if (anim_fixed_bot != NULL) {
    sel_bend_angle += delta;
    if (sel_bend_angle < 0)  sel_bend_angle = 0;
    if (sel_bend_angle > 90) sel_bend_angle = 90;
    enc_pending_angle = sel_bend_angle;      // store value
    enc_angle_update_pending = true;          // set flag
    printk("Bend angle: %d deg\n", sel_bend_angle);
    return;
}
```

### 3. Display thread: process the flag

In `display_update()`, after the existing `thick_update_pending` handler:

```c
if (enc_angle_update_pending) {
    enc_angle_update_pending = false;
    bend_anim_set_angle(enc_pending_angle);
}
```

### What Changes in the Execution Flow

**Before:**
```
enc_isr (interrupt)              display thread
───────────────────              ──────────────
bend_anim_set_angle()            lv_task_handler()
  lv_line_set_points() x4  ←──→   object tree traversal
  lv_label_set_text()              draw queue flush
  (concurrent, no lock)            (concurrent, no lock)
```

**After:**
```
enc_isr (interrupt)              display thread
───────────────────              ──────────────
enc_pending_angle = 45           display_update():
enc_angle_update_pending = true    bend_anim_set_angle(45)  ← sequential
return                             lv_task_handler()         ← sequential
```

The ISR returns in microseconds without touching LVGL. The display thread picks up the flag within 10ms (one display tick) and applies the angle change before calling `lv_task_handler()`. Both LVGL calls are now sequential on the same thread. No concurrent access. No corruption.

The visual result is identical — the angle animation updates within one display tick of the encoder rotation.
