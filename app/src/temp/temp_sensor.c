/*
 * temp_sensor.c — PT1000 RTD temperature sensing via ADC1_IN15 (PC5)
 *
 * Circuit: PT1000 in voltage divider → PC5 → ADC1 channel 15
 * Conversion:
 *   V   = raw * 3.3 / 4095
 *   R   = V / 0.000412        (divider current ~0.412 mA)
 *   T   = (R/1000 - 1) / 0.00385   (PT1000 linear approximation)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include "temp_sensor.h"

LOG_MODULE_REGISTER(temp_sensor, LOG_LEVEL_INF);

static double ema_temp = -999.0;  /* uninitialized sentinel */
#define EMA_ALPHA 0.2             /* 0.0 = no update, 1.0 = no filter */

static const struct device *adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc1));

static const struct adc_channel_cfg ch15_cfg = {
    .gain             = ADC_GAIN_1,
    .reference        = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id       = 15,
    .differential     = 0,
};

static int16_t sample_buf;
static struct adc_sequence seq = {
    .channels    = BIT(15),
    .buffer      = &sample_buf,
    .buffer_size = sizeof(sample_buf),
    .resolution  = 12,
};

static void temp_sensor_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC1 not ready");
        return;
    }

    int err = adc_channel_setup(adc_dev, &ch15_cfg);
    if (err) {
        LOG_ERR("ADC channel setup failed: %d", err);
        return;
    }

    while (1) {
        /* Average 16 samples, tracking min/max spread */
        int32_t sum = 0;
        int valid = 0;
        int mn = INT16_MAX, mx = INT16_MIN;
        for (int i = 0; i < 16; i++) {
            err = adc_read(adc_dev, &seq);
            if (err == 0) {
                int s = sample_buf;
                sum += s;
                if (s < mn) mn = s;
                if (s > mx) mx = s;
                valid++;
            }
            k_msleep(5);
        }

        if (valid == 0) {
            LOG_ERR("All ADC reads failed");
        } else {
            int avg = sum / valid;
            float V = avg * 3.3f / 4095.0f;
            float R = V / 0.000412f;
            float T = (R / 1000.0f - 1.0f) / 0.00385f;

            if (ema_temp < -900.0) {
                ema_temp = T;
            } else {
                ema_temp = EMA_ALPHA * T + (1.0 - EMA_ALPHA) * ema_temp;
            }

            LOG_INF("ADC avg=%d min=%d max=%d (spread=%d)", avg, mn, mx, mx - mn);
            LOG_INF("T_raw=%.1f T_ema=%.1f C", (double)T, ema_temp);
        }

        k_sleep(K_SECONDS(1));
    }
}

static K_THREAD_STACK_DEFINE(temp_stack, 1024);
static struct k_thread temp_thread_data;

void temp_thread_init(void)
{
    k_thread_create(&temp_thread_data, temp_stack, K_THREAD_STACK_SIZEOF(temp_stack),
                    temp_sensor_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
    k_thread_name_set(&temp_thread_data, "temp_sensor");
}
