/* display.c */
#include "display.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <zephyr/irq.h>
#include "../app_events.h"   /* event_post, g_inputs, system_event_t */

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/* ══════════════════════════════════════════════════════════════
 *  Geometry constants
 * ══════════════════════════════════════════════════════════════ */

#define ANIM_PIVOT_X    80
#define ANIM_PIVOT_Y   150
#define ANIM_BAR_LEN    60
#define ANIM_LAYER_GAP   6

#define THICK_BASE_Y    155
#define THICK_BAR_X      20
#define THICK_BAR_X2    140
#define THICK_ACRYLIC_Y 147

#define FWD_DEBOUNCE_MS     350
#define BCK_DEBOUNCE_MS     250

/* ══════════════════════════════════════════════════════════════
 *  Device handles
 * ══════════════════════════════════════════════════════════════ */

static const struct device *display_dev;

/* ══════════════════════════════════════════════════════════════
 *  Screen-ready synchronisation
 *  process_screen() gives this semaphore after building all LVGL
 *  objects.  run_countdown() takes it before its first g_progress
 *  write, so the display thread can never receive a progress update
 *  against a partially-constructed (or not-yet-constructed) screen.
 * ══════════════════════════════════════════════════════════════ */

K_SEM_DEFINE(display_screen_ready, 0, 1);

/* ══════════════════════════════════════════════════════════════
 *  Bend progress mailbox (separate from g_progress in app_events.h)
 *  Written by state thread via display_post_bend_progress(),
 *  consumed by display thread in display_update().
 * ══════════════════════════════════════════════════════════════ */

static struct {
    volatile bool pending;
    float fraction;
} g_bend_progress;

/* ══════════════════════════════════════════════════════════════
 *  Encoder state  (PC10 = A, PC11 = B)
 * ══════════════════════════════════════════════════════════════ */

static const struct device  *gpioc;
static volatile int32_t      enc_count = 0;
static int                   enc_raw   = 0;
static struct gpio_callback  enc_cb_data;

/* ══════════════════════════════════════════════════════════════
 *  Button state  (PB10 = forward, PB15 = backward)
 * ══════════════════════════════════════════════════════════════ */

static const struct device  *gpiob;
static struct gpio_callback  btn_fwd_cb_data;
static struct gpio_callback  btn_bck_cb_data;
static volatile bool         btn_fwd_pressed = false;
static volatile bool         btn_bck_pressed = false;

/* ══════════════════════════════════════════════════════════════
 *  Screen tracking
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    SM_SCREEN_DIRECTIONS = 0,
    SM_SCREEN_INPUT,
    SM_SCREEN_PROCESS,
    SM_SCREEN_COMPLETE,
    SM_SCREEN_ERROR
} sm_screen_t;

static sm_screen_t current_sm_screen    = SM_SCREEN_DIRECTIONS;
static bool        on_directions_screen = false;

/* ══════════════════════════════════════════════════════════════
 *  Input step state
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    INPUT_STEP_BEND_RADIUS = 0,
    INPUT_STEP_BEND_ANGLE,
    INPUT_STEP_THICKNESS,
    INPUT_STEP_COUNT
} input_step_t;

static input_step_t input_step     = INPUT_STEP_BEND_RADIUS;
static int          sel_bend_angle = 0;
static int          sel_thickness  = 0;   /* 0 = 1/16 in, 1 = 1/8 in */

/* ══════════════════════════════════════════════════════════════
 *  Bend animation elements
 * ══════════════════════════════════════════════════════════════ */

static lv_obj_t  *anim_fixed_bot;
static lv_obj_t  *anim_fixed_top;
static lv_obj_t  *anim_moving_bot;
static lv_obj_t  *anim_moving_top;
static lv_obj_t  *anim_value_label;
static lv_point_t anim_fixed_bot_pts[2];
static lv_point_t anim_fixed_top_pts[2];
static lv_point_t anim_moving_bot_pts[2];
static lv_point_t anim_moving_top_pts[2];

/* ══════════════════════════════════════════════════════════════
 *  Thickness animation elements
 * ══════════════════════════════════════════════════════════════ */

static lv_obj_t *thick_bar_line           = NULL;
static lv_obj_t *thick_value_label        = NULL;
static volatile bool thick_update_pending = false;

/* ── Deferred encoder angle update (ISR → display thread) ── */
static volatile bool enc_angle_update_pending = false;
static volatile int  enc_pending_angle        = 0;

/* ══════════════════════════════════════════════════════════════
 *  Process screen elements
 * ══════════════════════════════════════════════════════════════ */

static lv_obj_t *process_value_label = NULL;
static lv_obj_t *process_time_label  = NULL;
static lv_obj_t *process_pct_label   = NULL;
static lv_obj_t *bar                 = NULL;

/* ══════════════════════════════════════════════════════════════
 *  LVGL styles
 * ══════════════════════════════════════════════════════════════ */

static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_body;
static lv_style_t style_note;
static lv_style_t style_line_acrylic;
static lv_style_t style_line_wood;
static bool       styles_initialized = false;
static bool       line_styles_init   = false;

/* ══════════════════════════════════════════════════════════════
 *  Sin lookup table (scaled x1000, 0-90 degrees)
 * ══════════════════════════════════════════════════════════════ */

static const int16_t sin_lut[91] = {
       0,  17,  35,  52,  70,  87, 105, 122, 139, 156,
     174, 191, 208, 225, 242, 259, 276, 292, 309, 326,
     342, 358, 375, 391, 407, 423, 438, 454, 469, 485,
     500, 515, 530, 545, 559, 574, 588, 602, 616, 629,
     643, 656, 669, 682, 695, 707, 719, 731, 743, 755,
     766, 777, 788, 799, 809, 819, 829, 839, 848, 857,
     866, 874, 882, 891, 899, 906, 914, 921, 927, 934,
     940, 946, 951, 956, 961, 966, 970, 974, 978, 982,
     985, 988, 990, 993, 995, 996, 998, 999, 999,1000,
    1000
};
#define COS_LUT(a) sin_lut[90 - (a)]

/* ══════════════════════════════════════════════════════════════
 *  Forward declarations
 * ══════════════════════════════════════════════════════════════ */

static void bend_anim_set_angle(int angle_deg);
static void thick_anim_set(int idx);
static void direction_screen(void);
static void process_screen(const char *header, const char *value);
int input_selection_next(void);
int input_selection_prev(void);

/* ══════════════════════════════════════════════════════════════
 *  Encoder ISR  — PC11 falling edge
 * ══════════════════════════════════════════════════════════════ */

static void enc_isr(const struct device *dev, struct gpio_callback *cb,
                    uint32_t pins)
{
    int a = gpio_pin_get(gpioc, 10);
    int b = gpio_pin_get(gpioc, 11);

    if (a != b) {
        enc_raw++;
    } else {
        enc_raw--;
    }

    int new_count = enc_raw / 2;
    if (new_count == enc_count) return;

    int delta = (new_count > enc_count) ? 1 : -1;
    enc_count = new_count;

    /* Bend angle screen — defer LVGL update to display thread */
    if (anim_fixed_bot != NULL) {
        sel_bend_angle += delta;
        if (sel_bend_angle < 0)  sel_bend_angle = 0;
        if (sel_bend_angle > 90) sel_bend_angle = 90;
        enc_pending_angle = sel_bend_angle;
        enc_angle_update_pending = true;
        printk("Bend angle: %d deg\n", sel_bend_angle);
        return;
    }

    /* Thickness screen */
    if (thick_bar_line != NULL) {
        sel_thickness ^= 1;
        thick_update_pending = true;
        printk("Thickness toggled: %s\n",
               sel_thickness == 1 ? "1/8 in" : "1/16 in");
        return;
    }
}

void encoder_init(void)
{
    gpioc = DEVICE_DT_GET(DT_NODELABEL(gpioc));
    if (!device_is_ready(gpioc)) {
        printk("GPIOC not ready\n");
        return;
    }
    gpio_pin_configure(gpioc, 10, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_configure(gpioc, 11, GPIO_INPUT | GPIO_PULL_UP);
    gpio_pin_interrupt_configure(gpioc, 11, GPIO_INT_EDGE_FALLING);
    gpio_init_callback(&enc_cb_data, enc_isr, BIT(11));
    gpio_add_callback(gpioc, &enc_cb_data);
    printk("Encoder: init (PC10=A, PC11=B, IRQ on PC11)\n");
}

/* ══════════════════════════════════════════════════════════════
 *  Button ISRs — flags only, LVGL touched in display_update
 * ══════════════════════════════════════════════════════════════ */

static void btn_fwd_isr(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins)
{
    static int64_t last_press = 0;
    int64_t now = k_uptime_get();

    if (gpio_pin_get(gpiob, 10) == 0) {
        return;
    }
    if ((now - last_press) < FWD_DEBOUNCE_MS) {
        return;
    }
    last_press = now;

    printk("=== FORWARD pressed (step %d) ===\n", input_step);
    btn_fwd_pressed = true;
}

static void btn_bck_isr(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins)
{
    static int64_t last_press = 0;
    int64_t now = k_uptime_get();

    if (gpio_pin_get(gpiob, 15) == 0) {
        return;
    }

    if ((now - last_press) < BCK_DEBOUNCE_MS) {
        return;
    }
    last_press = now;

    printk("=== BACK pressed (step %d) ===\n", input_step);
    btn_bck_pressed = true;
}

void buttons_init(void)
{
    gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    if (!device_is_ready(gpiob)) {
        printk("GPIOB not ready\n");
        return;
    }
    gpio_pin_configure(gpiob, 10, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_pin_configure(gpiob, 15, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_pin_interrupt_configure(gpiob, 10, GPIO_INT_EDGE_RISING);
    gpio_pin_interrupt_configure(gpiob, 15, GPIO_INT_EDGE_RISING);
    gpio_init_callback(&btn_fwd_cb_data, btn_fwd_isr, BIT(10));
    gpio_init_callback(&btn_bck_cb_data, btn_bck_isr, BIT(15));
    gpio_add_callback(gpiob, &btn_fwd_cb_data);
    gpio_add_callback(gpiob, &btn_bck_cb_data);

    z_arm_irq_priority_set(EXTI15_10_IRQn, 1, 0);
    printk("Buttons: init (PB10=forward, PB15=backward)\n");
}

/* ══════════════════════════════════════════════════════════════
 *  Button flag handler — called from display_update
 * ══════════════════════════════════════════════════════════════ */

static void button_pressed(void)
{
    if (btn_fwd_pressed) {
        btn_fwd_pressed = false;

        /* Directions screen -> enter input selection */
        if (on_directions_screen) {
            printk(">>> Forward from directions\n");
            event_post(EVT_START_INIT);
            return;
        }

        /* Complete screen -> restart */
        if (current_sm_screen == SM_SCREEN_COMPLETE) {
            printk(">>> Forward from complete — restarting\n");
            event_post(EVT_START_IDLE);
            return;
        }

       /* Process screen — buttons do nothing during heating/bending/cooling */
        if (current_sm_screen == SM_SCREEN_PROCESS) {
            printk(">>> Button ignored during process screen\n");
            return;
        }

        /* Input selection screens */
        printk(">>> Forward (step %d -> %d)\n", input_step, input_step + 1);
        int ret = input_selection_next();
        if (ret == 1) {
            printk(">>> Inputs confirmed: angle=%d deg thickness=%s\n",
                   sel_bend_angle,
                   sel_thickness == 1 ? "1/8 in" : "1/16 in");
            g_inputs.bend_angle = sel_bend_angle;
            g_inputs.thickness  = sel_thickness;
            event_post(EVT_START_HEAT);
        } else {
            printk(">>> Advanced to step %d\n", input_step);
        }
    }

    if (btn_bck_pressed) {
        btn_bck_pressed = false;

        if (on_directions_screen || current_sm_screen == SM_SCREEN_COMPLETE) {
            return;
        }

        /* Process screen — back button also does nothing */
        if (current_sm_screen == SM_SCREEN_PROCESS) {
            printk(">>> Button ignored during process screen\n");
            return;
        }

        printk(">>> Back (step %d -> %d)\n", input_step, input_step - 1);
        int ret = input_selection_prev();
        if (ret == -1) {
            printk(">>> Cancelled — back to directions\n");
            event_post(EVT_START_IDLE);
            direction_screen();
        } else {
            printk(">>> Returned to step %d\n", input_step);
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Screen element cleanup
 * ══════════════════════════════════════════════════════════════ */

K_TIMER_DEFINE(bend_anim_timer, NULL, NULL);

static void bend_angle_input_screen_exit(void)
{
    k_timer_stop(&bend_anim_timer);
    anim_fixed_bot   = NULL;
    anim_fixed_top   = NULL;
    anim_moving_bot  = NULL;
    anim_moving_top  = NULL;
    anim_value_label = NULL;
}

static void thick_anim_clear(void)
{
    thick_bar_line    = NULL;
    thick_value_label = NULL;
}

/* ══════════════════════════════════════════════════════════════
 *  Style init
 * ══════════════════════════════════════════════════════════════ */

static void init_styles(void)
{
    if (styles_initialized) return;

    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_black());
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);

    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, lv_color_white());
    lv_style_set_text_font(&style_title, &lv_font_montserrat_24);

    lv_style_init(&style_subtitle);
    lv_style_set_text_color(&style_subtitle, lv_color_white());
    lv_style_set_text_font(&style_subtitle, &lv_font_montserrat_18);

    lv_style_init(&style_body);
    lv_style_set_text_color(&style_body, lv_color_white());
    lv_style_set_text_font(&style_body, &lv_font_montserrat_14);

    lv_style_init(&style_note);
    lv_style_set_text_color(&style_note, lv_color_hex(0xAAAAAA));
    lv_style_set_text_font(&style_note, &lv_font_montserrat_12);

    styles_initialized = true;
}

static void init_line_styles(void)
{
    if (line_styles_init) return;

    lv_style_init(&style_line_wood);
    lv_style_set_line_color(&style_line_wood, lv_color_hex(0x888888));
    lv_style_set_line_width(&style_line_wood, 8);
    lv_style_set_line_rounded(&style_line_wood, true);

    lv_style_init(&style_line_acrylic);
    lv_style_set_line_color(&style_line_acrylic, lv_color_white());
    lv_style_set_line_width(&style_line_acrylic, 4);
    lv_style_set_line_rounded(&style_line_acrylic, true);

    line_styles_init = true;
}

/* ══════════════════════════════════════════════════════════════
 *  Screen clear
 * ══════════════════════════════════════════════════════════════ */

static void clear_screen(void)
{
    bend_angle_input_screen_exit();
    thick_anim_clear();
    bar                  = NULL;
    process_value_label  = NULL;
    process_time_label   = NULL;
    process_pct_label    = NULL;
    on_directions_screen = false;

    /* Flush any pending LVGL refr/task work before deleting objects.
     * Without this, LVGL's internal refresh queue can hold pointers to
     * objects we are about to free, causing a crash on the next
     * lv_task_handler() call. */
    lv_task_handler();

    lv_obj_clean(lv_scr_act());

    /* Remove previously-applied styles so we don't accumulate duplicates
     * across multiple screen transitions, then re-apply the base style. */
    lv_obj_remove_style_all(lv_scr_act());
    lv_obj_add_style(lv_scr_act(), &style_screen, 0);
}

/* ══════════════════════════════════════════════════════════════
 *  Label helper
 * ══════════════════════════════════════════════════════════════ */

static lv_obj_t *make_label(lv_obj_t *parent,
                             lv_style_t *style,
                             const char *text,
                             lv_align_t align,
                             lv_coord_t x_ofs,
                             lv_coord_t y_ofs)
{
    lv_obj_t *lbl = lv_label_create(parent);
    if (lbl == NULL) {
        printk("ERROR: lv_label_create returned NULL (LVGL pool exhausted?)\n");
        return NULL;
    }
    lv_label_set_text(lbl, text);
    lv_obj_add_style(lbl, style, 0);
    lv_obj_align(lbl, align, x_ofs, y_ofs);
    return lbl;
}

/* ══════════════════════════════════════════════════════════════
 *  Encoder public API
 * ══════════════════════════════════════════════════════════════ */

int encoder_get_position(void)  { return (int)enc_count; }
int encoder_get_direction(void) { return 0; }

/* ══════════════════════════════════════════════════════════════
 *  Bend animation
 * ══════════════════════════════════════════════════════════════ */

static void bend_anim_set_angle(int angle_deg)
{
    if (angle_deg < 0)  angle_deg = 0;
    if (angle_deg > 90) angle_deg = 90;

    int dx = (ANIM_BAR_LEN * COS_LUT(angle_deg)) / 1000;
    int dy = (ANIM_BAR_LEN * sin_lut[angle_deg]) / 1000;

    anim_fixed_bot_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_fixed_bot_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;

    anim_fixed_top_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_top_pts[0].y = ANIM_PIVOT_Y;
    anim_fixed_top_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_top_pts[1].y = ANIM_PIVOT_Y;

    int nx = (ANIM_LAYER_GAP * sin_lut[angle_deg]) / 1000;
    int ny = (ANIM_LAYER_GAP * COS_LUT(angle_deg)) / 1000;

    anim_moving_bot_pts[0].x = ANIM_PIVOT_X      + nx;
    anim_moving_bot_pts[0].y = ANIM_PIVOT_Y      + ny;
    anim_moving_bot_pts[1].x = ANIM_PIVOT_X - dx + nx;
    anim_moving_bot_pts[1].y = ANIM_PIVOT_Y - dy + ny;

    anim_moving_top_pts[0].x = ANIM_PIVOT_X;
    anim_moving_top_pts[0].y = ANIM_PIVOT_Y;
    anim_moving_top_pts[1].x = ANIM_PIVOT_X - dx;
    anim_moving_top_pts[1].y = ANIM_PIVOT_Y - dy;

    if (anim_fixed_bot  != NULL) lv_line_set_points(anim_fixed_bot,  anim_fixed_bot_pts,  2);
    if (anim_fixed_top  != NULL) lv_line_set_points(anim_fixed_top,  anim_fixed_top_pts,  2);
    if (anim_moving_bot != NULL) lv_line_set_points(anim_moving_bot, anim_moving_bot_pts, 2);
    if (anim_moving_top != NULL) lv_line_set_points(anim_moving_top, anim_moving_top_pts, 2);

    if (anim_value_label != NULL) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d deg", angle_deg);
        lv_label_set_text(anim_value_label, buf);
    }
}

static void bend_anim_create(lv_obj_t *parent)
{
    init_line_styles();

    anim_fixed_bot_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_fixed_bot_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_fixed_bot = lv_line_create(parent);
    lv_line_set_points(anim_fixed_bot, anim_fixed_bot_pts, 2);
    lv_obj_add_style(anim_fixed_bot, &style_line_wood, 0);

    anim_fixed_top_pts[0].x = ANIM_PIVOT_X;
    anim_fixed_top_pts[0].y = ANIM_PIVOT_Y;
    anim_fixed_top_pts[1].x = ANIM_PIVOT_X + ANIM_BAR_LEN;
    anim_fixed_top_pts[1].y = ANIM_PIVOT_Y;
    anim_fixed_top = lv_line_create(parent);
    lv_line_set_points(anim_fixed_top, anim_fixed_top_pts, 2);
    lv_obj_add_style(anim_fixed_top, &style_line_acrylic, 0);

    anim_moving_bot_pts[0].x = ANIM_PIVOT_X;
    anim_moving_bot_pts[0].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_moving_bot_pts[1].x = ANIM_PIVOT_X - ANIM_BAR_LEN;
    anim_moving_bot_pts[1].y = ANIM_PIVOT_Y + ANIM_LAYER_GAP;
    anim_moving_bot = lv_line_create(parent);
    lv_line_set_points(anim_moving_bot, anim_moving_bot_pts, 2);
    lv_obj_add_style(anim_moving_bot, &style_line_wood, 0);

    anim_moving_top_pts[0].x = ANIM_PIVOT_X;
    anim_moving_top_pts[0].y = ANIM_PIVOT_Y;
    anim_moving_top_pts[1].x = ANIM_PIVOT_X - ANIM_BAR_LEN;
    anim_moving_top_pts[1].y = ANIM_PIVOT_Y;
    anim_moving_top = lv_line_create(parent);
    lv_line_set_points(anim_moving_top, anim_moving_top_pts, 2);
    lv_obj_add_style(anim_moving_top, &style_line_acrylic, 0);

    anim_value_label = lv_label_create(parent);
    lv_label_set_text(anim_value_label, "0 deg");
    lv_obj_add_style(anim_value_label, &style_title, 0);
    lv_obj_align(anim_value_label, LV_ALIGN_CENTER, 60, 20);
}

/* ══════════════════════════════════════════════════════════════
 *  Thickness animation
 * ══════════════════════════════════════════════════════════════ */

static void thick_anim_set(int idx)
{
    if (thick_bar_line == NULL) return;
    lv_obj_set_style_line_width(thick_bar_line,
                                (idx == 1) ? 10 : 4,
                                LV_PART_MAIN);
    lv_obj_invalidate(thick_bar_line);
    if (thick_value_label != NULL) {
        lv_label_set_text(thick_value_label,
                          (idx == 1) ? "1/8 in" : "1/16 in");
    }
    printk("Thickness: %s\n", (idx == 1) ? "1/8 in" : "1/16 in");
}

static void thick_anim_create(lv_obj_t *parent)
{
    sel_thickness = 0;

    static lv_point_t base_pts[2] = {
        {THICK_BAR_X, THICK_BASE_Y},
        {THICK_BAR_X2, THICK_BASE_Y}
    };
    lv_obj_t *base_bar = lv_line_create(parent);
    lv_line_set_points(base_bar, base_pts, 2);
    lv_obj_set_style_line_color(base_bar, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_line_width(base_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(base_bar, true, LV_PART_MAIN);

    static lv_point_t acrylic_pts[2] = {
        {THICK_BAR_X, THICK_ACRYLIC_Y},
        {THICK_BAR_X2, THICK_ACRYLIC_Y}
    };
    thick_bar_line = lv_line_create(parent);
    lv_line_set_points(thick_bar_line, acrylic_pts, 2);
    lv_obj_set_style_line_color(thick_bar_line, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(thick_bar_line, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(thick_bar_line, true, LV_PART_MAIN);

    thick_value_label = lv_label_create(parent);
    lv_label_set_text(thick_value_label, "1/16 in");
    lv_obj_add_style(thick_value_label, &style_title, 0);
    lv_obj_align(thick_value_label, LV_ALIGN_CENTER, 60, 20);
}

/* ══════════════════════════════════════════════════════════════
 *  Display init
 * ══════════════════════════════════════════════════════════════ */

int display_init(void)
{
    printk("LCD init: starting\n");

    lv_init();

    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
    printk("LCD init: SPI ready: %d\n", device_is_ready(spi_dev));

    display_dev = DEVICE_DT_GET(DT_NODELABEL(ili9341));
    printk("LCD init: display ready: %d\n", device_is_ready(display_dev));
    if (!device_is_ready(display_dev)) {
        printk("Display not ready\n");
        return -1;
    }

    int ret = display_blanking_off(display_dev);
    printk("LCD init: blanking off returned %d\n", ret);

    struct display_capabilities caps;
    display_get_capabilities(display_dev, &caps);
    printk("Display ready: %dx%d\n", caps.x_resolution, caps.y_resolution);

    encoder_init();
    buttons_init();
    init_styles();

    printk("LCD init: complete\n");
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 *  SCREEN 1 — Directions  (STATE_IDLE)
 * ══════════════════════════════════════════════════════════════ */

static void direction_screen(void)
{
    printk("display: directions screen — enter\n");
    clear_screen();
    printk("display: directions screen — clear done\n");
    current_sm_screen    = SM_SCREEN_DIRECTIONS;
    on_directions_screen = true;

    make_label(lv_scr_act(), &style_subtitle, "Directions:",
               LV_ALIGN_TOP_LEFT, 20, 7);
    printk("display: directions screen — header done\n");

    lv_obj_t *steps = lv_label_create(lv_scr_act());
    if (steps == NULL) {
        printk("ERROR: steps label NULL\n");
        return;
    }
    lv_label_set_long_mode(steps, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(steps, lv_pct(90));
    lv_obj_set_height(steps, LV_SIZE_CONTENT);
    lv_label_set_text(steps,
        "1. Insert acrylic on left hand side and align to desired position.\n\n"
        "2. Clamp by turning upper clamp knob (1).\n\n"
        "3. Manually adjust knob (2) to desired bend radius and tighten nuts to lock.\n\n"
        "4. Hit " LV_SYMBOL_PLAY " to proceed and select inputs.");
    lv_obj_add_style(steps, &style_body, 0);
    lv_obj_align(steps, LV_ALIGN_TOP_LEFT, 30, 35);
    printk("display: directions screen — steps done\n");

    lv_obj_t *estop = lv_label_create(lv_scr_act());
    if (estop == NULL) {
        printk("ERROR: estop label NULL\n");
        return;
    }
    lv_label_set_long_mode(estop, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(estop, lv_pct(100));
    lv_label_set_text(estop, "* E-stop on right side of machine for emergency");
    lv_obj_add_style(estop, &style_body, 0);
    lv_obj_align(estop, LV_ALIGN_BOTTOM_LEFT, 13, -2);
    printk("display: directions screen — done\n");
}

/* ══════════════════════════════════════════════════════════════
 *  SCREEN 2 — Input selection  (STATE_INITIALIZATION)
 * ══════════════════════════════════════════════════════════════ */

static void bend_radius_screen(void)
{
    printk("display: bend radius screen\n");
    clear_screen();
    current_sm_screen = SM_SCREEN_INPUT;

    make_label(lv_scr_act(), &style_note, "1 / 3",
               LV_ALIGN_TOP_RIGHT, -10, 10);
    make_label(lv_scr_act(), &style_subtitle, "Set Bend Radius",
               LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *msg = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(msg, lv_pct(85));
    lv_obj_set_height(msg, LV_SIZE_CONTENT);
    lv_label_set_text(msg,
        "Before proceeding, manually adjust the bend radius knob (2) "
        "to your desired radius and tighten the locking nuts to secure it.");
    lv_obj_add_style(msg, &style_body, 0);
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, 0);

    make_label(lv_scr_act(), &style_body,
               "Hit " LV_SYMBOL_PLAY " when ready\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void bend_angle_input_screen(void)
{
    printk("display: bend angle input screen\n");
    clear_screen();
    current_sm_screen = SM_SCREEN_INPUT;

    make_label(lv_scr_act(), &style_note, "2 / 3",
               LV_ALIGN_TOP_RIGHT, -10, 10);
    make_label(lv_scr_act(), &style_title, "Bend Angle",
               LV_ALIGN_CENTER, 60, -25);

    bend_anim_create(lv_scr_act());

    make_label(lv_scr_act(), &style_body,
               "Hit " LV_SYMBOL_PLAY " to proceed\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);

    sel_bend_angle = 0;
    bend_anim_set_angle(0);
}

static void thickness_screen(void)
{
    printk("display: thickness screen\n");
    clear_screen();
    current_sm_screen = SM_SCREEN_INPUT;

    make_label(lv_scr_act(), &style_note, "3 / 3",
               LV_ALIGN_TOP_RIGHT, -10, 10);
    make_label(lv_scr_act(), &style_title, "Thickness",
               LV_ALIGN_CENTER, 60, -25);

    thick_anim_create(lv_scr_act());

    make_label(lv_scr_act(), &style_body,
               "Turn to toggle thickness\n"
               "Hit " LV_SYMBOL_PLAY " to confirm\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

static void input_show_step(input_step_t step)
{
    switch (step) {
    case INPUT_STEP_BEND_RADIUS: bend_radius_screen();      break;
    case INPUT_STEP_BEND_ANGLE:  bend_angle_input_screen(); break;
    case INPUT_STEP_THICKNESS:   thickness_screen();        break;
    default: break;
    }
}

static void input_selection_enter(void)
{
    printk("display: entering input selection\n");
    input_step     = INPUT_STEP_BEND_RADIUS;
    sel_bend_angle = 0;
    sel_thickness  = 0;
    input_show_step(input_step);
}

int input_selection_next(void)
{
    if (input_step < INPUT_STEP_THICKNESS) {
        input_step++;
        input_show_step(input_step);
        return 0;
    }
    printk("Input confirmed — angle: %d deg, thickness: %s\n",
           sel_bend_angle, sel_thickness == 1 ? "1/8 in" : "1/16 in");
    return 1;
}

int input_selection_prev(void)
{
    if (input_step > INPUT_STEP_BEND_RADIUS) {
        input_step--;
        input_show_step(input_step);
        return 0;
    }
    printk("Input cancelled\n");
    return -1;
}

/* ══════════════════════════════════════════════════════════════
 *  SCREEN 3 — Process  (STATE_BEND / STATE_COOL)
 * ══════════════════════════════════════════════════════════════ */

static void process_screen(const char *header, const char *value)
{
    printk("display: process screen (%s)\n", header);
    clear_screen();
    current_sm_screen = SM_SCREEN_PROCESS;

    make_label(lv_scr_act(), &style_title, header, LV_ALIGN_CENTER, 0, -50);

    process_value_label = make_label(lv_scr_act(), &style_title, value,
                                     LV_ALIGN_CENTER, 0, -15);

    process_time_label = make_label(lv_scr_act(), &style_subtitle,
                                    "time remaining --:--",
                                    LV_ALIGN_CENTER, 0, 20);

    bar = lv_bar_create(lv_scr_act());
    lv_obj_set_size(bar, 200, 20);
    lv_obj_align(bar, LV_ALIGN_CENTER, 0, 50);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);

    /* Gray background track */
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x444444), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

    /* Green fill indicator */
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x00AA00), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);

    process_pct_label = make_label(lv_scr_act(), &style_body, "0%",
                                   LV_ALIGN_CENTER, 0, 75);

    /* Signal run_countdown() that all LVGL objects are live. */
    k_sem_give(&display_screen_ready);
}

/* ══════════════════════════════════════════════════════════════
 *  SCREEN 4 — Complete  (STATE_COMPLETE)
 * ══════════════════════════════════════════════════════════════ */

static void complete_screen(void)
{
    printk("display: complete screen\n");
    clear_screen();
    current_sm_screen = SM_SCREEN_COMPLETE;

    make_label(lv_scr_act(), &style_title, "Done!",
               LV_ALIGN_CENTER, 0, -20);
    make_label(lv_scr_act(), &style_body, "Safe to remove acrylic.",
               LV_ALIGN_CENTER, 0, 15);
    make_label(lv_scr_act(), &style_note,
               "Hit " LV_SYMBOL_PLAY " to start again",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

/* ══════════════════════════════════════════════════════════════
 *  SCREEN 5 — Error  (STATE_ERROR)
 * ══════════════════════════════════════════════════════════════ */

static void error_screen(const char *msg)
{
    printk("display: error screen\n");
    clear_screen();
    current_sm_screen = SM_SCREEN_ERROR;

    lv_obj_t *lbl = make_label(lv_scr_act(), &style_title, "ERROR",
                                LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFF4444), LV_PART_MAIN);

    make_label(lv_scr_act(), &style_body, msg,
               LV_ALIGN_CENTER, 0, 10);
    make_label(lv_scr_act(), &style_note,
               "Check machine and reset",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

/* ══════════════════════════════════════════════════════════════
 *  Public screen API
 * ══════════════════════════════════════════════════════════════ */

void display_create_home_screen(void) { direction_screen(); }

void display_set_state(int state)
{
    switch (state) {
        case STATE_IDLE:           direction_screen();                 break;
        case STATE_INITIALIZATION: input_selection_enter();            break;
        case STATE_HEAT:           process_screen("Heating...", "");   break;
        case STATE_BEND:           process_screen("Bending...", "");   break;
        case STATE_COOL:           process_screen("Cooling...", "");   break;
        case STATE_COMPLETE:       complete_screen();                  break;
        case STATE_ERROR:          error_screen("An error occurred."); break;
        case STATE_HOMING:         process_screen("Homing...", "");    break;
        default: break;
    }
}

/* ══════════════════════════════════════════════════════════════
 *  Live data update API
 * ══════════════════════════════════════════════════════════════ */

void display_update_value(const char *value)
{
    if (process_value_label != NULL)
        lv_label_set_text(process_value_label, value);
}

/* Called from display thread only — touches LVGL directly. */
static void display_update_progress(int pct, int min, int sec)
{
    if (bar == NULL) return;
    char time_buf[32];
    char pct_buf[8];
    snprintf(time_buf, sizeof(time_buf), "time remaining %02d:%02d", min, sec);
    snprintf(pct_buf,  sizeof(pct_buf),  "%d%%", pct);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    if (process_time_label != NULL) lv_label_set_text(process_time_label, time_buf);
    if (process_pct_label  != NULL) lv_label_set_text(process_pct_label,  pct_buf);
}

/* Called from state thread — posts to mailbox, display thread applies it. */
/* Called from state thread — posts bend progress to mailbox. */
void display_post_bend_progress(float fraction)
{
    g_bend_progress.fraction = fraction;
    g_bend_progress.pending  = true;
}

/* Called from display thread only — touches LVGL directly. */
static void display_update_bend_progress(float fraction)
{
    if (bar == NULL) return;
    int pct = (int)(fraction * 100.0f);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    char pct_buf[8];
    snprintf(pct_buf, sizeof(pct_buf), "%d%%", pct);
    lv_bar_set_value(bar, pct, LV_ANIM_OFF);
    if (process_pct_label  != NULL) lv_label_set_text(process_pct_label,  pct_buf);
    if (process_time_label != NULL) lv_label_set_text(process_time_label, "bending...");
}

/* ══════════════════════════════════════════════════════════════
 *  Periodic update — call every ~10 ms from display thread
 * ══════════════════════════════════════════════════════════════ */

void display_update(void)
{
    button_pressed();

    if (thick_update_pending) {
        thick_update_pending = false;
        thick_anim_set(sel_thickness);
    }

    /* Handle deferred encoder angle updates from enc_isr() */
    if (enc_angle_update_pending) {
        enc_angle_update_pending = false;
        bend_anim_set_angle(enc_pending_angle);
    }

    /* Handle countdown progress updates from run_countdown() */
    if (g_progress.pending) {
        g_progress.pending = false;
        __DMB();
        display_update_progress(g_progress.pct, g_progress.min, g_progress.sec);
    }

    /* Handle bend-fraction progress updates from state thread */
    if (g_progress.bend_pending) {
        g_progress.bend_pending = false;
        display_update_bend_progress(g_progress.bend_fraction);
    }

    /* Handle bend progress updates from state thread */
    if (g_bend_progress.pending) {
        g_bend_progress.pending = false;
        display_update_bend_progress(g_bend_progress.fraction);
    }

    lv_task_handler();
}