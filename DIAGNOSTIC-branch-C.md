# Branch C: Fix LVGL ISR Reentrancy

**Branch**: `test/fix-lvgl-isr-reentrancy`  
**Hypothesis**: The encoder ISR calling LVGL functions from interrupt context corrupts LVGL internal state.  
**Parent**: `ruben/find-homing-err` (`93fdb7a2`)

## What Changed

**One file modified**: `app/src/display/display.c`

The encoder ISR (`enc_isr`) previously called `bend_anim_set_angle()` directly — which calls `lv_line_set_points()` x4 and `lv_label_set_text()` x1 from interrupt context. LVGL is not reentrant, so if the ISR fires while `lv_task_handler()` is running in the display thread, internal state can corrupt.

**Change**: The ISR now sets a flag and stores the angle value. The display thread picks it up in `display_update()` and calls `bend_anim_set_angle()` from thread context. This follows the same pattern already used for `thick_update_pending`.

| Location | Old | New |
|---|---|---|
| `enc_isr()` (line ~216) | `bend_anim_set_angle(sel_bend_angle)` | `enc_pending_angle = sel_bend_angle; enc_angle_update_pending = true;` |
| `display_update()` (new block) | — | Checks `enc_angle_update_pending`, calls `bend_anim_set_angle(enc_pending_angle)` |

**DRV8452 bit-bang is unchanged** — still active on PB5-PB8. This isolates whether the ISR reentrancy is the cause independent of the pin conflict hypothesis.

## What to Observe

Run the standard test procedure from `DIAGNOSTIC.md` Section 9. Pay special attention to the bend angle input screen (step 2/3) — rotate the encoder while observing whether the animation updates smoothly.

- **Display survives** → LVGL reentrancy was the cause (or a contributing factor). Keep this fix permanently.
- **Display crashes** → ISR calls weren't the cause. Focus on Branch A results.

## Note

This fix is correct regardless of test outcome. Calling LVGL from ISR context is always wrong. This change should be merged even if it doesn't fix the specific display crash under investigation.
