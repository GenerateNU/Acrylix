/*
 * drv8452_spi.c — DRV8452SPWPR SPI diagnostic + initialization
 *
 * Reads back key registers before and after writing EN_OUT so we can
 * see the exact device state over RTT. Call drv8452_spi_init() once
 * at startup before stepper_init().
 *
 * SPI2 wiring (J502 jumper wires):
 *   PB12 = nSCS  (cs-gpios, GPIO_ACTIVE_LOW)
 *   PB13 = SCK
 *   PB14 = MISO
 *   PB15 = MOSI
 *
 * SPI frame (16-bit, MSB first):
 *   Bit 15   : 0 (reserved)
 *   Bit 14   : R/W  (0 = write, 1 = read)
 *   Bits 13:8: address (6 bits)
 *   Bits 7:0 : data (0x00 on read command)
 *
 * Response on SDO during the same 16-bit frame:
 *   Bits 15:8: status byte (mirrors FAULT register)
 *   Bits 7:0 : register data
 *
 * NOTE — SPI mode: Mode 1 (CPOL=0 CPHA=1).
 * DRV8452 samples SDI on the rising edge; SCK idles low.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/printk.h>
#include "drv8452_spi.h"

/* DRV8452 device node (child of spi2 in app.overlay) */
#define DRV_NODE DT_NODELABEL(drv8452)

/* Mode 1: CPOL=0 (idle low), CPHA=1 (sample on second edge) */
static const struct spi_dt_spec drv_spi = SPI_DT_SPEC_GET(
    DRV_NODE,
    SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_MODE_CPHA,
    0
);

/* ── Register addresses ────────────────────────────────────────────────── */
#define REG_FAULT   0x00u
#define REG_DIAG1   0x01u
#define REG_DIAG2   0x02u
#define REG_CTRL1   0x04u
#define REG_CTRL2   0x05u

/* CTRL1: EN_OUT=1, keep reset defaults (TOFF=0b11, DECAY=0b11) → 0x8F */
#define CTRL1_EN_OUT_SET  0x8Fu

/* ── Low-level transfer helpers ────────────────────────────────────────── */

static int drv_write_reg(uint8_t addr, uint8_t data)
{
    /* Write frame: bit14=0 (write), bits[13:8]=addr, bits[7:0]=data */
    uint8_t tx[2] = {
        (uint8_t)(addr & 0x3Fu),
        data
    };
    const struct spi_buf     tx_buf = { .buf = tx, .len = sizeof(tx) };
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    return spi_write_dt(&drv_spi, &tx_set);
}

/*
 * drv_read_reg — send a read command and capture the 16-bit response.
 *
 * status_out: bits[15:8] of the response (mirrors FAULT register); may be NULL
 * data_out:   bits[7:0]  of the response (register contents); may be NULL
 */
static int drv_read_reg(uint8_t addr, uint8_t *status_out, uint8_t *data_out)
{
    /* Read frame: bit14=1 (read), bits[13:8]=addr, bits[7:0]=0x00 */
    uint8_t tx[2] = {
        (uint8_t)(0x40u | (addr & 0x3Fu)),
        0x00u
    };
    uint8_t rx[2] = { 0u, 0u };

    const struct spi_buf     tx_buf = { .buf = tx, .len = sizeof(tx) };
    const struct spi_buf     rx_buf = { .buf = rx, .len = sizeof(rx) };
    const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
    const struct spi_buf_set rx_set = { .buffers = &rx_buf, .count = 1 };

    int ret = spi_transceive_dt(&drv_spi, &tx_set, &rx_set);
    if (ret == 0) {
        if (status_out) { *status_out = rx[0]; }
        if (data_out)   { *data_out   = rx[1]; }
    }
    return ret;
}

/* ── Per-register decode printers ──────────────────────────────────────── */

static void print_fault(uint8_t v)
{
    printk("  FAULT  =0x%02X"
           "  [FAULT=%d SPI_ERR=%d UVLO=%d CPUV=%d OCP=%d STL=%d TF=%d OL=%d]\n",
           v,
           (v >> 7) & 1,   /* FAULT   — any fault active          */
           (v >> 6) & 1,   /* SPI_ERR — bad SPI frame received     */
           (v >> 5) & 1,   /* UVLO    — undervoltage lockout       */
           (v >> 4) & 1,   /* CPUV    — charge pump undervoltage   */
           (v >> 3) & 1,   /* OCP     — overcurrent                */
           (v >> 2) & 1,   /* STL     — motor stall                */
           (v >> 1) & 1,   /* TF      — thermal fault              */
           (v >> 0) & 1);  /* OL      — open load                  */
}

static void print_diag1(uint8_t v)
{
    printk("  DIAG1  =0x%02X"
           "  [OCP_LS2_B=%d OCP_HS2_B=%d OCP_LS1_B=%d OCP_HS1_B=%d"
           " OCP_LS2_A=%d OCP_HS2_A=%d OCP_LS1_A=%d OCP_HS1_A=%d]\n",
           v,
           (v >> 7) & 1,
           (v >> 6) & 1,
           (v >> 5) & 1,
           (v >> 4) & 1,
           (v >> 3) & 1,
           (v >> 2) & 1,
           (v >> 1) & 1,
           (v >> 0) & 1);
}

static void print_diag2(uint8_t v)
{
    /* bit 6 reserved */
    printk("  DIAG2  =0x%02X"
           "  [STSL=%d OTW=%d OTS=%d STALL=%d LRN_DONE=%d OL_B=%d OL_A=%d]\n",
           v,
           (v >> 7) & 1,   /* STSL     — stall detection learning  */
           (v >> 5) & 1,   /* OTW      — overtemperature warning    */
           (v >> 4) & 1,   /* OTS      — overtemperature shutdown   */
           (v >> 3) & 1,   /* STALL                                 */
           (v >> 2) & 1,   /* LRN_DONE — stall learn complete       */
           (v >> 1) & 1,   /* OL_B     — open load bridge B         */
           (v >> 0) & 1);  /* OL_A     — open load bridge A         */
}

static void print_ctrl1(uint8_t v)
{
    printk("  CTRL1  =0x%02X"
           "  [EN_OUT=%d SR=%d IDX_RST=%d TOFF=%d DECAY=%d]\n",
           v,
           (v >> 7) & 1,       /* EN_OUT  — output bridge enable    */
           (v >> 6) & 1,       /* SR      — slew rate               */
           (v >> 5) & 1,       /* IDX_RST — reset step index        */
           (v >> 2) & 0x7u,    /* TOFF[4:2] — off-time setting (3b) */
           (v >> 0) & 0x3u);   /* DECAY[1:0]                        */
}

static void print_ctrl2(uint8_t v)
{
    printk("  CTRL2  =0x%02X"
           "  [MICROSTEP_MODE=%d]\n",
           v,
           (v >> 0) & 0xFu);  /* MICROSTEP_MODE[3:0] */
}

/* Read all diagnostic/config registers and print them */
static int snapshot_registers(void)
{
    uint8_t status, data;
    int ret;

    ret = drv_read_reg(REG_FAULT, &status, &data);
    if (ret != 0) {
        printk("  [SPI transfer failed, ret=%d]\n", ret);
        return ret;
    }
    print_fault(data);

    ret = drv_read_reg(REG_DIAG1, &status, &data);
    if (ret == 0) { print_diag1(data); }

    ret = drv_read_reg(REG_DIAG2, &status, &data);
    if (ret == 0) { print_diag2(data); }

    ret = drv_read_reg(REG_CTRL1, &status, &data);
    if (ret == 0) { print_ctrl1(data); }

    ret = drv_read_reg(REG_CTRL2, &status, &data);
    if (ret == 0) { print_ctrl2(data); }

    return 0;
}

/* ── Public entry point ────────────────────────────────────────────────── */

int drv8452_spi_init(void)
{
    if (!spi_is_ready_dt(&drv_spi)) {
        printk("DRV8452: SPI2 not ready — check overlay and prj.conf CONFIG_SPI\n");
        return -ENODEV;
    }

    /* ── SPI bus sanity check ─────────────────────────────────────────────
     * Issue one read before touching any register.
     * - If spi_transceive_dt returns non-zero: bus error (PB12-PB15 wiring,
     *   pinctrl, or clock domain not enabled).
     * - If both rx bytes are 0xFF: MISO is floating — device absent,
     *   unpowered, or CS not asserting.
     * - If both are 0x00: MISO stuck low — possible short or wrong CS pin.
     */
    {
        uint8_t status = 0, data = 0;
        int ret = drv_read_reg(REG_FAULT, &status, &data);
        if (ret != 0) {
            printk("DRV8452: *** SPI bus error (ret=%d) — "
                   "check PB12-PB15 wiring and SPI2 clock domain ***\n", ret);
            return ret;
        }
        if (status == 0xFFu && data == 0xFFu) {
            printk("DRV8452: *** MISO reads 0xFF/0xFF — "
                   "device absent, unpowered, or CS not asserting ***\n");
            /* Fall through — print all registers to confirm pattern */
        } else if (status == 0x00u && data == 0x00u) {
            printk("DRV8452: *** MISO reads 0x00/0x00 — "
                   "possible MISO short to GND or wrong SPI mode ***\n");
        } else {
            printk("DRV8452: SPI bus OK (status=0x%02X data=0x%02X)\n",
                   status, data);
        }
    }

    /* ── Pre-write snapshot ──────────────────────────────────────────────── */
    printk("DRV8452: === registers BEFORE EN_OUT write ===\n");
    snapshot_registers();

    /* ── Write CTRL1: EN_OUT=1 ───────────────────────────────────────────── */
    int ret = drv_write_reg(REG_CTRL1, CTRL1_EN_OUT_SET);
    if (ret != 0) {
        printk("DRV8452: CTRL1 write failed (ret=%d)\n", ret);
        return ret;
    }

    /* ── Post-write snapshot ─────────────────────────────────────────────── */
    printk("DRV8452: === registers AFTER EN_OUT write ===\n");
    snapshot_registers();

    /* ── Verify EN_OUT was accepted ──────────────────────────────────────── */
    {
        uint8_t status = 0, ctrl1 = 0;
        drv_read_reg(REG_CTRL1, &status, &ctrl1);
        if ((ctrl1 & 0x80u) == 0u) {
            printk("DRV8452: *** EN_OUT reads back 0 after writing 1! "
                   "SPI write not taking effect ***\n");
            printk("          Likely causes:\n");
            printk("          1. Wrong SPI mode — try removing SPI_MODE_CPOL "
                   "(use Mode 1: CPOL=0 CPHA=1)\n");
            printk("          2. CS polarity wrong — scope nSCS to confirm "
                   "it pulses LOW during transfer\n");
            printk("          3. MOSI not reaching device — check PB15 continuity\n");
            return -EIO;
        }
        printk("DRV8452: EN_OUT=1 confirmed — output bridges enabled\n");
    }

    return 0;
}
