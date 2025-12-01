/*
 * MMC5983MA Magnetometer Driver - Core Functions
 * SPDX-License-Identifier: Apache-2.0
 *
 * These functions contain the testable logic (data conversion, validation)
 * that can run on native_sim without hardware.
 */

#include "mmc5983ma.h"
#include <math.h>

/**
 * @brief Convert raw register bytes to 18-bit signed values
 *
 * The MMC5983MA outputs 18-bit values split across registers:
 * - X: XOUT0[7:0], XOUT1[7:0], XYZOUT2[7:6]
 * - Y: YOUT0[7:0], YOUT1[7:0], XYZOUT2[5:4]
 * - Z: ZOUT0[7:0], ZOUT1[7:0], XYZOUT2[3:2]
 *
 * Raw values are unsigned with offset at 131072 (2^17)
 */
void mmc5983ma_convert_raw_bytes(const uint8_t *raw_bytes,
                                 struct mmc5983ma_raw_data *data) {
  uint32_t x_raw, y_raw, z_raw;

  /* Combine bytes into 18-bit unsigned values */
  /* Byte order: [0]=XOUT0, [1]=XOUT1, [2]=YOUT0, [3]=YOUT1,
   *             [4]=ZOUT0, [5]=ZOUT1, [6]=XYZOUT2 */
  x_raw = ((uint32_t)raw_bytes[0] << 10) | ((uint32_t)raw_bytes[1] << 2) |
          ((raw_bytes[6] >> 6) & 0x03);

  y_raw = ((uint32_t)raw_bytes[2] << 10) | ((uint32_t)raw_bytes[3] << 2) |
          ((raw_bytes[6] >> 4) & 0x03);

  z_raw = ((uint32_t)raw_bytes[4] << 10) | ((uint32_t)raw_bytes[5] << 2) |
          ((raw_bytes[6] >> 2) & 0x03);

  /* Convert to signed by subtracting offset */
  data->x = (int32_t)x_raw - MMC5983MA_OFFSET;
  data->y = (int32_t)y_raw - MMC5983MA_OFFSET;
  data->z = (int32_t)z_raw - MMC5983MA_OFFSET;
}

/**
 * @brief Convert raw counts to Gauss
 *
 * MMC5983MA sensitivity: 1 LSB = 0.0625 mG = 0.0000625 G
 */
void mmc5983ma_convert_to_gauss(const struct mmc5983ma_raw_data *raw,
                                struct mmc5983ma_data *data) {
  data->x = (float)raw->x * MMC5983MA_LSB_TO_GAUSS;
  data->y = (float)raw->y * MMC5983MA_LSB_TO_GAUSS;
  data->z = (float)raw->z * MMC5983MA_LSB_TO_GAUSS;
  data->magnitude = mmc5983ma_calculate_magnitude(data->x, data->y, data->z);
}

/**
 * @brief Validate product ID
 */
int mmc5983ma_validate_product_id(uint8_t product_id) {
  if (product_id == MMC5983MA_PRODUCT_ID) {
    return 0;
  }
  return -1;
}

/**
 * @brief Calculate magnetic field magnitude
 */
float mmc5983ma_calculate_magnitude(float x, float y, float z) {
  return sqrtf(x * x + y * y + z * z);
}
