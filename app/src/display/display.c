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

LOG_MODULE_REGISTER(display, LOG_LEVEL_INF);

/* ── Bend animation geometry ── */
#define ANIM_PIVOT_X   80
#define ANIM_PIVOT_Y   150
#define ANIM_BAR_LEN   60
#define ANIM_LAYER_GAP 6

#define DEBOUNCE_MS 300

/* ── Display device ── */
static const struct device *display_dev;

/* ── UI encoder state (PC10=A, PC11=B) ── */
static const struct device  *gpioc;
static volatile int32_t      enc_count = 0;
static struct gpio_callback  enc_cb_data;
static int position = 0;
static int direction = 0;
static K_MUTEX_DEFINE(encoder_mutex);

/* ── Button state (PB10=forward, PB15=backward) ── */
static const struct device  *gpiob;
static struct gpio_callback  btn_fwd_cb_data;
static struct gpio_callback  btn_bck_cb_data;
static volatile bool btn_fwd_pressed = false;
static volatile bool btn_bck_pressed = false;

/* ── UI elements ── */
static lv_obj_t *bar;
static bool on_radius_screen = false;

/* ── Bend animation elements ── */
static lv_obj_t           *anim_fixed_bot;
static lv_obj_t           *anim_fixed_top;
static lv_obj_t           *anim_moving_bot;
static lv_obj_t           *anim_moving_top;
static lv_obj_t           *anim_value_label;
static lv_point_t  anim_fixed_bot_pts[2];
static lv_point_t  anim_fixed_top_pts[2];
static lv_point_t  anim_moving_bot_pts[2];
static lv_point_t  anim_moving_top_pts[2];
static int                 sel_bend_angle = 0;

/* ── Thickness animation elements ── */
static lv_obj_t           *thick_wood_bot;
static lv_obj_t           *thick_wood_top;
static lv_obj_t           *thick_acrylic_bot;
static lv_obj_t           *thick_acrylic_top;
static lv_obj_t           *thick_value_label;
static lv_point_t  thick_wood_bot_pts[2];
static lv_point_t  thick_wood_top_pts[2];
static lv_point_t  thick_acrylic_bot_pts[2];
static lv_point_t  thick_acrylic_top_pts[2];
/* 0 = 1/16 mm, 1 = 1/8 mm */
static int sel_thickness = 0;
static lv_obj_t *thick_bar_line = NULL;
static volatile bool thick_update_pending = false;

/* ── LVGL styles ── */
static lv_style_t style_screen;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_body;
static lv_style_t style_note;
static lv_style_t style_line_acrylic;
static lv_style_t style_line_wood;
static bool styles_initialized = false;
static bool line_styles_init   = false;

typedef enum {
    INPUT_STEP_BEND_RADIUS = 0,
    INPUT_STEP_BEND_ANGLE,
    INPUT_STEP_THICKNESS,
    INPUT_STEP_COUNT
} input_step_t;

static input_step_t input_step = INPUT_STEP_BEND_RADIUS;

/* ── sin lookup table (scaled to 1000) for degrees ── */
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

static void bend_anim_set_angle(int angle_deg);   /* forward declaration */
static void thick_anim_set(int idx);              /* forward declaration */
int input_selection_next(void);
int input_selection_prev(void);

/* ── UI encoder ISR — PC11 triggers on both edges ── */
static int enc_raw = 0;

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

    /* Only update every 2 raw pulses — adjust to 1, 2, or 4 to match detent feel */
    int new_count = enc_raw / 2;
    if (new_count == enc_count) {
        return;
    }
    int delta = (new_count > enc_count) ? 1 : -1;
    enc_count = new_count;

    /* Bend angle screen */
    if (anim_fixed_bot != NULL) {
        sel_bend_angle += delta;
        if (sel_bend_angle < 0)  sel_bend_angle = 0;
        if (sel_bend_angle > 90) sel_bend_angle = 90;
        bend_anim_set_angle(sel_bend_angle);
        printk("Bend angle: %d deg\n", sel_bend_angle);
        return;
    }

    /* Thickness screen — toggle between 1/16 and 1/8 on any encoder movement */
    if (thick_bar_line != NULL) {
        sel_thickness ^= 1;
        thick_update_pending = true;
        printk("Thickness toggled: %s\n", sel_thickness == 1 ? "1/8 in" : "1/16 in");
        return;
    }

    position += delta;
    direction = delta;
    printk("Encoder position: %d direction: %d\n", position, direction);
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
    printk("Encoder: init (PC10=A, PC11=B, IRQ on PC11/EXTI11)\n");
}

static void btn_fwd_isr(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins)
{
    static int64_t last_press = 0;
    int64_t now = k_uptime_get();
    if ((now - last_press) < DEBOUNCE_MS) {
        return;
    }
    last_press = now;

    if (gpio_pin_get(gpiob, 10) == 0) {
        printk("=== FORWARD button pressed (step %d) ===\n", input_step);
        btn_fwd_pressed = true;
    }
}

static void btn_bck_isr(const struct device *dev, struct gpio_callback *cb,
                         uint32_t pins)
{
    static int64_t last_press = 0;
    int64_t now = k_uptime_get();
    if ((now - last_press) < DEBOUNCE_MS) {
        return;
    }
    last_press = now;

    if (gpio_pin_get(gpiob, 15) == 0) {
        printk("=== BACK button pressed (step %d) ===\n", input_step);
        btn_bck_pressed = true;
    }
}

void buttons_init(void)
{
    gpiob = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    if (!device_is_ready(gpiob)) {
        printk("GPIOB not ready\n");
        return;
    }
    gpio_pin_configure(gpiob, 10, GPIO_INPUT);   //forward
    gpio_pin_configure(gpiob, 15, GPIO_INPUT);   //backward

    gpio_pin_interrupt_configure(gpiob, 10, GPIO_INT_EDGE_FALLING);
    gpio_pin_interrupt_configure(gpiob, 15, GPIO_INT_EDGE_FALLING);

    gpio_init_callback(&btn_fwd_cb_data, btn_fwd_isr, BIT(10));
    gpio_init_callback(&btn_bck_cb_data, btn_bck_isr, BIT(15));

    gpio_add_callback(gpiob, &btn_fwd_cb_data);
    gpio_add_callback(gpiob, &btn_bck_cb_data);

    printk("Buttons: init (PB10=forward, PB15=backward)\n");
}

void button_pressed(void)
{
    if (btn_fwd_pressed) {
        btn_fwd_pressed = false;
        printk("=== FORWARD button pressed (step %d -> %d) ===\n",
               input_step, input_step + 1);
        int ret = input_selection_next();
        if (ret == 1) {
            printk(">>> All inputs confirmed, ready to bend\n");
        } else {
            printk(">>> Advanced to step %d\n", input_step);
        }
    }

    if (btn_bck_pressed) {
        btn_bck_pressed = false;
        printk("=== BACK button pressed (step %d -> %d) ===\n",
               input_step, input_step - 1);
        int ret = input_selection_prev();
        if (ret == -1) {
            printk(">>> Cancelled — returning to directions screen\n");
            direction_screen();
        } else {
            printk(">>> Returned to step %d\n", input_step);
        }
    }
}
K_TIMER_DEFINE(bend_anim_timer, NULL, NULL);

/* bend angle clear screen */
static void bend_angle_input_screen_exit(void)
{
    k_timer_stop(&bend_anim_timer);
    anim_fixed_bot   = NULL;
    anim_fixed_top   = NULL;
    anim_moving_bot  = NULL;
    anim_moving_top  = NULL;
    anim_value_label = NULL;
}

/* thickness clear screen */
static void thick_anim_clear(void)
{
    thick_wood_bot    = NULL;
    thick_wood_top    = NULL;
    thick_acrylic_bot = NULL;
    thick_acrylic_top = NULL;
    thick_value_label = NULL;
    thick_bar_line    = NULL;
}

/* intialize UI styles */
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
    lv_style_set_text_color(&style_note, lv_color_white());
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

/* clear screen function */
static void clear_screen(void)
{
    bend_angle_input_screen_exit();
    thick_anim_clear();
    bar = NULL;
    on_radius_screen = false;
    lv_obj_clean(lv_scr_act());
    lv_obj_add_style(lv_scr_act(), &style_screen, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent,
                             lv_style_t *style,
                             const char *text,
                             lv_align_t align,
                             lv_coord_t x_ofs,
                             lv_coord_t y_ofs)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_add_style(lbl, style, 0);
    lv_obj_align(lbl, align, x_ofs, y_ofs);
    return lbl;
}

/* encoder API */

int encoder_get_position(void) { return (int)enc_count; }

int encoder_get_direction(void) { return 0; }

/* bend angle animation */

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

/* thickness animation */
static void thick_anim_set(int idx)
{
    if (thick_bar_line == NULL) return;

    /* Thicker line sits higher to grow upward from the gray base */
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

    /* Gray base bar — fixed, same style as bend angle wood bar */
    static lv_point_t base_pts[2] = {
        {20, 155},
        {140, 155}
    };
    lv_obj_t *base_bar = lv_line_create(parent);
    lv_line_set_points(base_bar, base_pts, 2);
    lv_obj_set_style_line_color(base_bar, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_obj_set_style_line_width(base_bar, 8, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(base_bar, true, LV_PART_MAIN);

    /* White acrylic bar — sits on top of gray bar, changes thickness */
    static lv_point_t acrylic_pts[2] = {
        {20, 147},
        {140, 147}
    };
    thick_bar_line = lv_line_create(parent);
    lv_line_set_points(thick_bar_line, acrylic_pts, 2);
    lv_obj_set_style_line_color(thick_bar_line, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_line_width(thick_bar_line, 4, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(thick_bar_line, true, LV_PART_MAIN);

    /* Value label to the right, same position as bend angle label */
    thick_value_label = lv_label_create(parent);
    lv_label_set_text(thick_value_label, "1/16\"");
    lv_obj_add_style(thick_value_label, &style_title, 0);
    lv_obj_align(thick_value_label, LV_ALIGN_CENTER, 60, 20);
}

/* intialize display screens */

int display_init(void)
{
    printk("LCD init: starting\n");

    const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
    printk("LCD init: SPI device get: %p\n", (void *)spi_dev);
    printk("LCD init: SPI ready: %d\n", device_is_ready(spi_dev));

    display_dev = DEVICE_DT_GET(DT_NODELABEL(ili9341));
    printk("LCD init: display device get: %p\n", (void *)display_dev);
    printk("LCD init: display ready: %d\n", device_is_ready(display_dev));
    if (!device_is_ready(display_dev)) {
        printk("Display not ready\n");
        printk("LCD init: complete, returning %d\n", -1);
        return -1;
    }

    printk("LCD init: calling blanking off\n");
    int ret = display_blanking_off(display_dev);
    printk("LCD init: blanking off returned %d\n", ret);

    struct display_capabilities caps;
    display_get_capabilities(display_dev, &caps);
    printk("Display ready: %dx%d\n", caps.x_resolution, caps.y_resolution);

    /* intialize encoder and buttons */
    encoder_init();
    buttons_init();


    init_styles();
    printk("LCD init: complete, returning %d\n", 0);
    return 0;
}

/* SCREEN 1 — direction */
void direction_screen(void)
{
    printk("display: directions screen\n");
    clear_screen();

    make_label(lv_scr_act(), &style_subtitle, "Directions:",
               LV_ALIGN_TOP_LEFT, 30, 20);

    lv_obj_t *steps = lv_label_create(lv_scr_act());
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

    lv_obj_t *estop = lv_label_create(lv_scr_act());
    lv_label_set_long_mode(estop, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(estop, lv_pct(100));
    lv_label_set_text(estop,
        "* E-stop on right side of machine for emergency");
    lv_obj_add_style(estop, &style_note, 0);
    lv_obj_align(estop, LV_ALIGN_BOTTOM_LEFT, 13, -18);
}

/* SCREEN 2 — BEND RADIUS REMINDER */
void bend_radius_screen(void)
{
    printk("display: bend radius screen\n");
    clear_screen();
    on_radius_screen = true;

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
    lv_obj_align(msg, LV_ALIGN_CENTER, 0, -10);

    make_label(lv_scr_act(), &style_note,
               "Hit " LV_SYMBOL_PLAY " when ready\n"
               "Hit " LV_SYMBOL_PREV " to return",
               LV_ALIGN_BOTTOM_MID, 0, -15);
}

/* SCREEN 3 — bend angle input */
void bend_angle_input_screen(void)
{
    printk("display: bend angle input screen\n");
    clear_screen();

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

/* SCREEN 4 — acrylic thickness selection */
void thickness_screen(void)
{
    printk("display: thickness screen\n");
    clear_screen();

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
    case INPUT_STEP_BEND_RADIUS:
        bend_radius_screen();
        break;
    case INPUT_STEP_BEND_ANGLE:
        bend_angle_input_screen();
        break;
    case INPUT_STEP_THICKNESS:
        thickness_screen();
        break;
    default:
        break;
    }
}

void input_selection_enter(void)
{
    input_step = INPUT_STEP_BEND_RADIUS;
    input_show_step(input_step);
}

int input_selection_next(void)
{
    if (input_step < INPUT_STEP_THICKNESS) {
        input_step++;
        input_show_step(input_step);
        return 0;
    }
    /* Last step confirmed */
    printk("Input confirmed — bend angle: %d deg, thickness: %s\n",
           sel_bend_angle, sel_thickness == 1 ? "1/8 mm" : "1/16 mm");
    return 1;
}

int input_selection_prev(void)
{
    if (input_step > INPUT_STEP_BEND_RADIUS) {
        input_step--;
        input_show_step(input_step);
        return 0;
    }
    /* First step — signal exit to state machine */
    printk("Input cancelled\n");
    return -1;
}

void display_create_home_screen(void) { direction_screen(); }

void display_update(void)
{
    button_pressed();

    if (thick_update_pending) {
        thick_update_pending = false;
        thick_anim_set(sel_thickness);
    }

    lv_task_handler();
}
