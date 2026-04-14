#pragma once

/**
 * drv8452_spi.h — One-time SPI init for DRV8452SPWPR (SPI variant)
 *
 * The SPI variant requires EN_OUT=1 in CTRL1 (reg 0x04) before any
 * STEP pulses are sent, otherwise the output bridges remain Hi-Z.
 *
 * Call drv8452_spi_init() once at startup, before stepper_init().
 *
 * Returns 0 on success, negative errno on failure.
 */
int  drv8452_spi_init(void);

/* Write EN_OUT=1 to CTRL1 — enables output bridges. */
void drv8452_enable(void);

/* Write EN_OUT=0 to CTRL1 — disables output bridges (Hi-Z). */
void drv8452_disable(void);
