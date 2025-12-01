/*
 * MMC5983MA Magnetometer Driver
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MMC5983MA_H
#define MMC5983MA_H

#include <stdint.h>

/* MMC5983MA I2C Address */
#define MMC5983MA_ADDR          0x30

/* Register definitions */
#define MMC5983MA_REG_XOUT0     0x00
#define MMC5983MA_REG_XOUT1     0x01
#define MMC5983MA_REG_YOUT0     0x02
#define MMC5983MA_REG_YOUT1     0x03
#define MMC5983MA_REG_ZOUT0     0x04
#define MMC5983MA_REG_ZOUT1     0x05
#define MMC5983MA_REG_XYZOUT2   0x06
#define MMC5983MA_REG_TOUT      0x07
#define MMC5983MA_REG_STATUS    0x08
#define MMC5983MA_REG_CTRL0     0x09
#define MMC5983MA_REG_CTRL1     0x0A
#define MMC5983MA_REG_CTRL2     0x0B
#define MMC5983MA_REG_PRODUCT_ID 0x2F

/* Expected Product ID */
#define MMC5983MA_PRODUCT_ID    0x30

/* Control register bits */
#define MMC5983MA_CTRL0_TM      0x01  /* Trigger measurement */
#define MMC5983MA_CTRL0_SET     0x08  /* SET operation */
#define MMC5983MA_CTRL0_RESET   0x10  /* RESET operation */
#define MMC5983MA_CTRL1_BW_100HZ 0x00 /* Bandwidth 100Hz */
#define MMC5983MA_CTRL2_CMM_EN  0x10  /* Continuous measurement mode */

/* Conversion constants */
#define MMC5983MA_OFFSET        131072  /* 18-bit midpoint (2^17) */
#define MMC5983MA_LSB_TO_GAUSS  0.0000625f  /* 1 LSB = 0.0625 mG */

/**
 * @brief Magnetometer data structure (raw counts)
 */
struct mmc5983ma_raw_data {
	int32_t x;
	int32_t y;
	int32_t z;
};

/**
 * @brief Magnetometer data structure (in Gauss)
 */
struct mmc5983ma_data {
	float x;
	float y;
	float z;
	float magnitude;
};

/**
 * @brief Convert raw register bytes to 18-bit signed values
 *
 * @param raw_bytes Array of 7 bytes from registers 0x00-0x06
 * @param data Output structure with signed x, y, z values
 */
void mmc5983ma_convert_raw_bytes(const uint8_t *raw_bytes,
				 struct mmc5983ma_raw_data *data);

/**
 * @brief Convert raw counts to Gauss
 *
 * @param raw Raw magnetometer data (signed counts)
 * @param data Output structure with values in Gauss
 */
void mmc5983ma_convert_to_gauss(const struct mmc5983ma_raw_data *raw,
				struct mmc5983ma_data *data);

/**
 * @brief Validate product ID
 *
 * @param product_id Product ID read from sensor
 * @return 0 if valid, -1 if invalid
 */
int mmc5983ma_validate_product_id(uint8_t product_id);

/**
 * @brief Calculate magnetic field magnitude
 *
 * @param x X-axis value in Gauss
 * @param y Y-axis value in Gauss
 * @param z Z-axis value in Gauss
 * @return Magnitude in Gauss
 */
float mmc5983ma_calculate_magnitude(float x, float y, float z);

#endif /* MMC5983MA_H */
