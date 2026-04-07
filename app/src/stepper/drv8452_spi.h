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
int drv8452_spi_init(void);
