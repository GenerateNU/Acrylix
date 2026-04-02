#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>

/* ── Devicetree handles ─────────────────────────────────────── */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

static const struct pwm_dt_spec step_pwm =
    PWM_DT_SPEC_GET(ZEPHYR_USER_NODE);                          /* PB0 TIM3_CH3 */
static const struct gpio_dt_spec dir_pin =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, dir_gpios);              /* PB1 */
static const struct gpio_dt_spec enable_pin =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enable_gpios);           /* PC0 */
static const struct gpio_dt_spec nfault_pin =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, nfault_gpios);           /* PC2 */

/* Encoder inputs from AM26LS32ACDR differential line receiver */
static const struct gpio_dt_spec enc_a =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_a_gpios);            /* PA0 */
static const struct gpio_dt_spec enc_b =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_b_gpios);            /* PA1 */
static const struct gpio_dt_spec enc_z =
    GPIO_DT_SPEC_GET(ZEPHYR_USER_NODE, enc_z_gpios);            /* PA2 */

/* nSLEEP is hardwired to +3.3V on PCB — no GPIO handle needed. */

/* SPI bus (DRV8452 advanced features) */
static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct spi_config spi_cfg = {
    .frequency = 5000000,
    /* DRV8452: Mode 3 (CPOL=1, CPHA=1), MSB first, 8-bit words */
    .operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB |
                 SPI_MODE_CPOL | SPI_MODE_CPHA,
    .slave     = 0,
    .cs        = SPI_CS_CONTROL_INIT(DT_NODELABEL(spi1), 0),
};

/* ── Motion constants ───────────────────────────────────────── */
#define STEPS_PER_REV  400UL   /* 1/2 step × 200 full steps/rev */
#define TARGET_RPM     30.0f
#define PERIOD_NS      ((uint32_t)((60.0f / TARGET_RPM / STEPS_PER_REV) * 1e9f))
#define PULSE_NS       (PERIOD_NS / 2u)
#define DIR_FORWARD    1
#define DIR_BACKWARD   0

static long g_current_steps = 0;

/* ─────────────────────────────────────────────────────────────
 * DRV8452 SPI helpers
 *
 * 16-bit frame (MSB first):
 *   bit15      : 0 = write, 1 = read
 *   bits[14:9] : 6-bit register address
 *   bits[7:0]  : 8-bit data
 *
 * Registers:
 *   0x02 CTRL2  — MICROSTEP_MODE[3:0]
 *   0x03 CTRL3  — DECAY[6:4], TBLANK_TIME[3:2], TOFF[1:0]
 *   0x0D CTRL13 — ATQ_EN (auto-torque)
 *   0x0E CTRL14 — EN_STSL (standstill power saving)
 *
 * VREF_INT_EN intentionally left 0: VREF is set by external
 * R_REF1/R_REF2 divider (≈4.24V → I_peak ≈3.21A).
 * ──────────────────────────────────────────────────────────── */
#define DRV_REG_CTRL2   0x02
#define DRV_REG_CTRL3   0x03
#define DRV_REG_CTRL13  0x0D
#define DRV_REG_CTRL14  0x0E

static int drv8452_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)((reg & 0x3F) << 1), val };
    struct spi_buf     buf    = { .buf = tx, .len = 2 };
    struct spi_buf_set tx_set = { .buffers = &buf, .count = 1 };
    return spi_write(spi_dev, &spi_cfg, &tx_set);
}

static int drv8452_read(uint8_t reg, uint8_t *val)
{
    uint8_t tx[2] = { (uint8_t)(0x80 | ((reg & 0x3F) << 1)), 0x00 };
    uint8_t rx[2] = { 0 };
    struct spi_buf tx_buf = { .buf = tx, .len = 2 };
    struct spi_buf rx_buf = { .buf = rx, .len = 2 };
    struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };
    int err = spi_transceive(spi_dev, &spi_cfg, &tx_set, &rx_set);
    if (!err) *val = rx[1];
    return err;
}

static int drv8452_init(void)
{
    int err = 0;

    /* CTRL2: MICROSTEP_MODE = 0011b → 1/2 step (400 steps/rev) */
    err |= drv8452_write(DRV_REG_CTRL2, 0x03);

    /* CTRL3: DECAY=111b (Smart Tune Ripple Control), TBLANK=01b, TOFF=01b
     *        = 0b_111_01_01 = 0x75 */
    err |= drv8452_write(DRV_REG_CTRL3, 0x75);

    /* CTRL13: ATQ_EN=1 → auto-torque (reduces heat at light load) */
    err |= drv8452_write(DRV_REG_CTRL13, 0x01);

    /* CTRL14: EN_STSL=1 → standstill power saving after ~64ms idle */
    err |= drv8452_write(DRV_REG_CTRL14, 0x01);

    return err;
}

/* ── Motion helpers ─────────────────────────────────────────── */
static long degrees_to_steps(float deg)
{
    return (long)((deg / 360.0f) * (float)STEPS_PER_REV);
}

static void move_to_steps(long target)
{
    long delta = target - g_current_steps;
    if (delta == 0) return;

    if (gpio_pin_get_dt(&nfault_pin) == 0) {
        printk("DRV8452 FAULT — move aborted\n");
        return;
    }

    gpio_pin_set_dt(&dir_pin, delta > 0 ? DIR_FORWARD : DIR_BACKWARD);
    k_msleep(1); /* DIR setup: min 200 ns per datasheet */

    long     steps   = (delta > 0) ? delta : -delta;
    uint32_t wait_us = (uint32_t)(((uint64_t)steps * PERIOD_NS) / 1000u);

    pwm_set_dt(&step_pwm, PERIOD_NS, PULSE_NS);
    k_busy_wait(wait_us);
    pwm_set_dt(&step_pwm, PERIOD_NS, 0u);

    g_current_steps = target;
}

static void move_to_degrees(float deg)
{
    move_to_steps(degrees_to_steps(deg));
}

/* ── Main ───────────────────────────────────────────────────── */
int main(void)
{
    /* Readiness checks */
    if (!pwm_is_ready_dt(&step_pwm))    { printk("PWM not ready\n");    return -1; }
    if (!gpio_is_ready_dt(&dir_pin))    { printk("DIR not ready\n");    return -1; }
    if (!gpio_is_ready_dt(&enable_pin)) { printk("ENABLE not ready\n"); return -1; }
    if (!gpio_is_ready_dt(&nfault_pin)) { printk("nFAULT not ready\n"); return -1; }
    if (!gpio_is_ready_dt(&enc_a))      { printk("ENC_A not ready\n");  return -1; }
    if (!gpio_is_ready_dt(&enc_b))      { printk("ENC_B not ready\n");  return -1; }
    if (!gpio_is_ready_dt(&enc_z))      { printk("ENC_Z not ready\n");  return -1; }
    if (!device_is_ready(spi_dev))      { printk("SPI not ready\n");    return -1; }

    /* Configure outputs */
    gpio_pin_configure_dt(&dir_pin,    GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&enable_pin, GPIO_OUTPUT_INACTIVE); /* bridge off until init done */

    /* Configure inputs */
    gpio_pin_configure_dt(&nfault_pin, GPIO_INPUT);  /* external 10kΩ pullup on PCB */
    gpio_pin_configure_dt(&enc_a,      GPIO_INPUT);
    gpio_pin_configure_dt(&enc_b,      GPIO_INPUT);
    gpio_pin_configure_dt(&enc_z,      GPIO_INPUT);

    /* nSLEEP hardwired high — chip already awake.
     * Wait for SPI-ready: datasheet tWAKE = 0.25ms max. */
    k_msleep(2);

    int err = drv8452_init();
    if (err) {
        printk("DRV8452 SPI init failed: %d\n", err);
        return -1;
    }
    printk("DRV8452 init OK\n");

    /* Enable bridge outputs (overrides R102 pulldown) */
    gpio_pin_set_dt(&enable_pin, 1);

    printk("STEP : PB0 (TIM3_CH3)\n");
    printk("DIR  : PB1\n");
    printk("Steps/rev : %lu\n", STEPS_PER_REV);
    printk("Period    : %u ns (%u us)\n", PERIOD_NS, PERIOD_NS / 1000u);
    printk("VREF      : external ~4.24V -> I_peak ~3.21A\n");

    k_msleep(2000);

    float positions[] = { 0, 15, 30, 45, 60, 75, 90, 105, 120, 135, 150, 165, 180 };
    int   num_pos     = sizeof(positions) / sizeof(positions[0]);

    while (1) {
        for (int i = 0; i < num_pos; i++) {
            printk("-> %.0f deg  ENC_A=%d B=%d Z=%d\n",
                   (double)positions[i],
                   gpio_pin_get_dt(&enc_a),
                   gpio_pin_get_dt(&enc_b),
                   gpio_pin_get_dt(&enc_z));
            move_to_degrees(positions[i]);
            k_msleep(500);
        }
        k_msleep(1000);
        for (int i = num_pos - 1; i >= 0; i--) {
            printk("-> %.0f deg  ENC_A=%d B=%d Z=%d\n",
                   (double)positions[i],
                   gpio_pin_get_dt(&enc_a),
                   gpio_pin_get_dt(&enc_b),
                   gpio_pin_get_dt(&enc_z));
            move_to_degrees(positions[i]);
            k_msleep(500);
        }
        k_msleep(1000);
    }
    return 0;
}
