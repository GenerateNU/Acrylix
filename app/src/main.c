/* main.c */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <lvgl.h>
#include "display/display.h"
#include "states/states.h"
//#include "stepper/stepper.h"
//#include "temp/temp.h"

//LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* ── Testing hardware ──────────────────────────────────────────────────── */
const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
//const struct device *enc = DEVICE_DT_GET(DT_NODELABEL(encoder));

/* ── Input callback (testing) ──────────────────────────────────────────── */
static void any_input_cb(struct input_event *evt, void *user_data)
{
    printk("Input event: type=%d code=%d value=%d\n",
           evt->type, evt->code, evt->value);
}
INPUT_CALLBACK_DEFINE(NULL, any_input_cb, NULL);

/* ── Thread stack sizes & priorities ───────────────────────────────────── */
#define STATE_STACK_SIZE    2048
#define STATE_PRIORITY      7

#define DISPLAY_STACK_SIZE  1024
#define DISPLAY_PRIORITY    6

/*
#define TEMP_STACK_SIZE     1024
#define TEMP_PRIORITY       5 */

/* ── Thread stack allocation ───────────────────────────────────────────── */
K_THREAD_STACK_DEFINE(state_stack, STATE_STACK_SIZE);
static struct k_thread state_thread_data;

K_THREAD_STACK_DEFINE(display_stack, DISPLAY_STACK_SIZE);
static struct k_thread display_thread_data;

/* K_THREAD_STACK_DEFINE(temp_stack, TEMP_STACK_SIZE);
static struct k_thread temp_thread_data; */

/* ── Thread entry functions ────────────────────────────────────────────── */
void state_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    printk("State machine thread started\n");

    printk("Initial state: %s\n", get_state_name(g_sm.previous));

    while (1) {
    //add error events later
       switch (g_sm.current) {
            case STATE_IDLE:
                /* add input_handler and display_handler*/
                k_sleep(K_SECONDS(10));
                sm_transition(STATE_INITIALIZATION);
                /* --- use when events are needed
                if (evt == EVT_START_INIT){
                    sm_transition(STATE_INITIALIZATION);
                } */
                break;
            case STATE_INITIALIZATION:
                /* add homing_handler */
                /* add temp_handler and stepper_handler */
                k_sleep(K_SECONDS(5));
                sm_transition(STATE_BEND);
                break;
            case STATE_BEND:
                /* add bend_handler */
                k_sleep(K_SECONDS(5));
                sm_transition(STATE_COOL);
                break;
            case STATE_COOL:
                /* add cooling_handler */
                k_sleep(K_SECONDS(5));
                sm_transition(STATE_COMPLETE);
                break;
            case STATE_COMPLETE:
                k_sleep(K_SECONDS(5));
                sm_transition(STATE_HOMING);
                break;
            case STATE_HOMING:
                k_sleep(K_SECONDS(5));
                sm_transition(STATE_IDLE);
                break;
            case STATE_ERROR:
                k_sleep(K_SECONDS(10));
                sm_transition(STATE_IDLE);
                break;
            default:
                printk("Unknown state: %d", g_sm.current);
                //sm_set_error(ERROR_NONE);
                break;
        }

        k_sleep(K_MSEC(100));
    }
}

/* display thread*/
void display_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    printk("Display thread started\n");

    while (1) {
        display_create_home_screen();
        k_sleep(K_MSEC(100));
    }
}

void stepper_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    printk("stepper thread started\n");

    while (1) {
        //stepper_handler_run();
        k_sleep(K_MSEC(100));
    }
}

/* 
void temp_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);
    printk("Temperature thread started\n");

    while (1) { */
        /* TODO: call temperature_handler_run() */
        //k_sleep(K_MSEC(500));   /* 2Hz — temp changes slowly */
    //} 
//}

/* ── Main ──────────────────────────────────────────────────────────────── */
int main(void)
{
    printk("project starting...\n");

    /* Initialize state machine */
    sm_init();

    /* Initialize UI */
    /* TODO: display_init() */
    display_init();

    if (display_init() != 0) {
        printk("could not initialize display\n");
        return -1;
    }

     /* Initialize input */

    /* Initialize stepper */


    /* Initialize temperature sensor */
 

    /* Create state thread */
    k_tid_t state_tid = k_thread_create(
        &state_thread_data, state_stack,
        K_THREAD_STACK_SIZEOF(state_stack),
        state_thread, NULL, NULL, NULL,
        STATE_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(state_tid, "state");
    printk("State machine thread created\n");

    /* Create display thread */
    k_tid_t display_tid = k_thread_create(
        &display_thread_data, display_stack,
        K_THREAD_STACK_SIZEOF(display_stack),
        display_thread, NULL, NULL, NULL,
        DISPLAY_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(display_tid, "display");
    printk("Display thread created\n");

    /* Create temperature thread */
    
    /*k_tid_t temp_tid = k_thread_create(
        &temp_thread_data, temp_stack,
        K_THREAD_STACK_SIZEOF(temp_stack),
        temp_thread, NULL, NULL, NULL,
        TEMP_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(temp_tid, "temperature");
    printk("Temperature thread created\n");*/

    return 0;

    /* ── LCD testing ──────────────────────────────────────────────────────
    if (display_init() != 0) {
        printk("could not initialize display\n");
        gpio_pin_toggle_dt(&led);
        k_msleep(200);
        return -1;
    }
    display_create_home_screen();
    while (1) {
        display_update();
        k_msleep(10);
    }
    */

    /* ── LED testing ──────────────────────────────────────────────────────
    gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
    while (1) {
        gpio_pin_toggle_dt(&led);
        k_msleep(500);
    }
    */
}