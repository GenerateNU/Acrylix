/*
 * drv8452_spi.c — DRV8452SPWPR bit-bang SPI driver
 *
 * Hardware SPI2 (PB12-PB15) unusable — top-row J502 traces are physically
 * dead. All four signals remapped to working bottom-row pins via GPIO bit-bang.
 *
 * Pin assignments (bottom-row J502):
 *   PB6  = nCS   (GPIO output, active low)
 *   PB7  = SCLK  (GPIO output, idle low)
 *   PB8  = MOSI  (GPIO output)        *** overlaps MIPI DBI dc-gpios — display broken ***
 *   PB5  = MISO  (GPIO input)         *** PB5 smoke LED removed from main.c ***
 *
 * WARNING: PB6 also serves as SPI1 cs-gpios for the ILI9341 display, and PB8
 * serves as the MIPI DBI DC pin. The display will not work while these pins are
 * claimed for bit-bang. Restore SPI2/hardware SPI when traces are repaired.
 *
 * SPI frame (16-bit, MSB first):
 *   Bit 15   : 0 (reserved)
 *   Bit 14   : R/W  (0 = write, 1 = read)
 *   Bits 13:8: address (6 bits)
 *   Bits 7:0 : data  (0x00 on read command)
 *
 * Response on MISO during same 16-bit frame:
 *   Bits 15:8: status byte (mirrors FAULT register)
 *   Bits 7:0 : register data
 *
 * SPI Mode 1 (CPOL=0, CPHA=1): clock idles low, DRV8452 captures MOSI on
 * falling edge, shifts out MISO on rising edge.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include "drv8452_spi.h"

/* ── Pin assignments ────────────────────────────────────────────────────── */
#define SPI_NCS_PIN   6   /* PB6  — nCS,  active low                        */
#define SPI_SCLK_PIN  7   /* PB7  — SCLK, idle low                          */
#define SPI_MOSI_PIN  8   /* PB8  — MOSI                                     */
#define SPI_MISO_PIN  5   /* PB5  — MISO (freed from smoke LED in main.c)   */

static const struct device *bb_gpio;

/* ── Register addresses ────────────────────────────────────────────────── */
#define REG_FAULT   0x00u
#define REG_CTRL1   0x04u
#define REG_CTRL2   0x05u

/* CTRL1: EN_OUT=1, keep reset defaults (TOFF=0b11, DECAY=0b11) → 0x8F */
#define CTRL1_EN_OUT_SET  0x8Fu
/* CTRL1: EN_OUT=0, same TOFF/DECAY defaults → 0x0F */
#define CTRL1_EN_OUT_CLR  0x0Fu
/* CLR_FLT: bit 6 of CTRL1 — write 1 to clear latched faults, auto-resets to 0 */
#define CLR_FLT_BIT       0x40u
/* CTRL2: MICROSTEP_MODE = 0000b = full step 100% current */
#define CTRL2_FULL_STEP   0x00u

/* ── Bit-bang SPI ───────────────────────────────────────────────────────── */

static void bb_spi_init(void)
{
    bb_gpio = DEVICE_DT_GET(DT_NODELABEL(gpiob));
    gpio_pin_configure(bb_gpio, SPI_NCS_PIN,  GPIO_OUTPUT_HIGH);
    gpio_pin_configure(bb_gpio, SPI_SCLK_PIN, GPIO_OUTPUT_LOW);
    gpio_pin_configure(bb_gpio, SPI_MOSI_PIN, GPIO_OUTPUT_LOW);
    gpio_pin_configure(bb_gpio, SPI_MISO_PIN, GPIO_INPUT);
}

/*
 * Mode 1 (CPOL=0, CPHA=1): clock idles low.
 * MOSI is set before the rising edge.
 * DRV8452 captures MOSI on falling edge; shifts MISO out on rising edge.
 * We sample MISO after the falling edge (data stable).
 */
static uint16_t bb_spi_transfer(uint16_t tx)
{
    uint16_t rx = 0;

    gpio_pin_set(bb_gpio, SPI_NCS_PIN, 0);   /* assert CS */
    k_busy_wait(2);                           /* tSU_nSCS >= 2 us */

    for (int i = 15; i >= 0; i--) {
        /* Set MOSI before rising edge */
        gpio_pin_set(bb_gpio, SPI_MOSI_PIN, (tx >> i) & 1);
        k_busy_wait(1);

        /* Rising edge — DRV8452 shifts MISO out */
        gpio_pin_set(bb_gpio, SPI_SCLK_PIN, 1);
        k_busy_wait(1);

        /* Falling edge — DRV8452 captures MOSI, we capture MISO */
        gpio_pin_set(bb_gpio, SPI_SCLK_PIN, 0);
        rx |= ((uint16_t)gpio_pin_get(bb_gpio, SPI_MISO_PIN) << i);
        k_busy_wait(1);
    }

    gpio_pin_set(bb_gpio, SPI_NCS_PIN, 1);   /* deassert CS */
    k_busy_wait(2);
    return rx;
}

/* ── Register read/write helpers ────────────────────────────────────────── */

static uint16_t drv_read_raw(uint8_t addr)
{
    uint16_t tx = (uint16_t)(0x40u | (addr & 0x3Fu)) << 8;
    return bb_spi_transfer(tx);
}

static void drv_write_raw(uint8_t addr, uint8_t data)
{
    uint16_t tx = ((uint16_t)(addr & 0x3Fu) << 8) | data;
    bb_spi_transfer(tx);
}

/* ── Public entry point ────────────────────────────────────────────────── */

int drv8452_spi_init(void)
{
    bb_spi_init();

    /* Dummy read to flush DRV8452 SPI state machine */
    bb_spi_transfer(0x4000u);

    /* Enable output bridges: write EN_OUT=1 to CTRL1 */
    drv_write_raw(REG_CTRL1, CTRL1_EN_OUT_SET);

    /* Verify EN_OUT was accepted */
    uint16_t rx = drv_read_raw(REG_CTRL1);
    uint8_t ctrl1 = rx & 0xFF;
    uint8_t en_out_readback = (ctrl1 >> 7) & 1u;
    printk("DRV8452: EN_OUT=%d\n", en_out_readback);

    if (!en_out_readback) {
        printk("DRV8452: EN_OUT write failed — CTRL1=0x%02X\n", ctrl1);
        return -EIO;
    }

    /* Set full step mode (100% current) before readback */
    drv_write_raw(REG_CTRL2, CTRL2_FULL_STEP);

    /* Read and decode CTRL2 microstep mode */
    uint16_t ctrl2_rx = drv_read_raw(REG_CTRL2);
    uint8_t ctrl2 = ctrl2_rx & 0xFF;
    uint8_t ms_mode = ctrl2 & 0x0F; /* MICROSTEP_MODE bits [3:0] */

    const char *ms_str;
    int ms_div;
    switch (ms_mode) {
        case 0x0: ms_str = "Full step 100%";    ms_div = 1;   break;
        case 0x1: ms_str = "Full step 71%";     ms_div = 1;   break;
        case 0x2: ms_str = "Non-circular 1/2";  ms_div = 2;   break;
        case 0x3: ms_str = "1/2";               ms_div = 2;   break;
        case 0x4: ms_str = "1/4";               ms_div = 4;   break;
        case 0x5: ms_str = "1/8";               ms_div = 8;   break;
        case 0x6: ms_str = "1/16";              ms_div = 16;  break;
        case 0x7: ms_str = "1/32";              ms_div = 32;  break;
        case 0x8: ms_str = "1/64";              ms_div = 64;  break;
        case 0x9: ms_str = "1/128";             ms_div = 128; break;
        case 0xA: ms_str = "1/256";             ms_div = 256; break;
        default:  ms_str = "unknown";            ms_div = 0;   break;
    }
    printk("DRV8452: CTRL2=0x%02X MICROSTEP_MODE=%d = %s (%d steps/rev)\n",
           ctrl2, ms_mode, ms_str, 200 * ms_div);

    /* Dump all CTRL and FAULT registers to identify CLR_FLT location and defaults */
    for (uint8_t reg = 0x00; reg <= 0x0Au; reg++) {
        uint16_t rx = drv_read_raw(reg);
        printk("DRV8452: reg[0x%02X] = 0x%02X (status=0x%02X)\n",
               reg, (uint8_t)(rx & 0xFF), (uint8_t)(rx >> 8));
    }

    printk("DRV8452: init OK\n");
    return 0;
}

void drv8452_enable(void)
{
    drv_write_raw(REG_CTRL1, CTRL1_EN_OUT_SET);
    printk("DRV8452: EN_OUT=1 (enabled)\n");
}

void drv8452_disable(void)
{
    drv_write_raw(REG_CTRL1, CTRL1_EN_OUT_CLR);
    printk("DRV8452: EN_OUT=0 (disabled)\n");
}

void drv8452_set_current(uint8_t ctrl2_val)
{
    drv_write_raw(REG_CTRL2, ctrl2_val);
    printk("DRV8452: CTRL2=0x%02X\n", ctrl2_val);
}

uint8_t drv8452_read_fault(void)
{
    uint16_t rx = drv_read_raw(REG_FAULT);
    return (uint8_t)(rx & 0xFF);
}

void drv8452_hold(void)
{
    uint8_t fault = drv8452_read_fault();
    if (fault) {
        /* Clear latched fault (e.g. stall from bend move) then re-assert EN_OUT */
        drv_write_raw(REG_CTRL1, CTRL1_EN_OUT_SET | CLR_FLT_BIT);
        k_busy_wait(100);
        drv_write_raw(REG_CTRL1, CTRL1_EN_OUT_SET);
        uint8_t fault_after = drv8452_read_fault();
        if (fault_after) {
            printk("DRV8452: HOLD — FAULT=0x%02X persistent after CLR_FLT\n", fault_after);
        }
    }
}
