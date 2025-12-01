/*
 * MMC5983MA Magnetometer Driver Unit Tests
 * SPDX-License-Identifier: Apache-2.0
 *
 * These tests verify the data conversion and validation logic
 * of the MMC5983MA driver without requiring actual hardware.
 */

#include <zephyr/ztest.h>
#include <math.h>
#include "mmc5983ma.h"

/* =============================================================================
 * Test Suite Setup
 * =============================================================================
 */

static void *mmc5983ma_suite_setup(void)
{
	printk("MMC5983MA Driver Unit Tests\n");
	return NULL;
}

/* =============================================================================
 * Product ID Validation Tests
 * =============================================================================
 */

/**
 * @brief Test that valid product ID (0x30) is accepted
 */
ZTEST(mmc5983ma_suite, test_validate_product_id_valid)
{
	int ret = mmc5983ma_validate_product_id(0x30);

	zassert_equal(ret, 0, "Valid product ID 0x30 should return 0");
}

/**
 * @brief Test that invalid product ID is rejected
 */
ZTEST(mmc5983ma_suite, test_validate_product_id_invalid)
{
	int ret = mmc5983ma_validate_product_id(0x00);

	zassert_equal(ret, -1, "Invalid product ID should return -1");

	ret = mmc5983ma_validate_product_id(0xFF);
	zassert_equal(ret, -1, "Invalid product ID 0xFF should return -1");

	ret = mmc5983ma_validate_product_id(0x31);
	zassert_equal(ret, -1, "Invalid product ID 0x31 should return -1");
}

/* =============================================================================
 * Raw Byte Conversion Tests
 * =============================================================================
 */

/**
 * @brief Test conversion of zero-field reading (midpoint values)
 *
 * When magnetic field is zero, the sensor outputs the midpoint value (131072)
 * which should convert to 0 after offset subtraction.
 */
ZTEST(mmc5983ma_suite, test_convert_raw_bytes_zero_field)
{
	struct mmc5983ma_raw_data data;

	/* Midpoint = 131072 = 0x20000
	 * As 18-bit split: high 8 bits = 0x80, mid 8 bits = 0x00, low 2 bits = 0x00
	 */
	uint8_t raw_bytes[7] = {
		0x80, 0x00, /* X: high, mid */
		0x80, 0x00, /* Y: high, mid */
		0x80, 0x00, /* Z: high, mid */
		0x00        /* XYZ low bits all zero */
	};

	mmc5983ma_convert_raw_bytes(raw_bytes, &data);

	zassert_equal(data.x, 0, "Zero field X should be 0, got %d", data.x);
	zassert_equal(data.y, 0, "Zero field Y should be 0, got %d", data.y);
	zassert_equal(data.z, 0, "Zero field Z should be 0, got %d", data.z);
}

/**
 * @brief Test conversion of maximum positive field
 *
 * Maximum 18-bit value = 262143 (0x3FFFF)
 * After offset: 262143 - 131072 = 131071
 */
ZTEST(mmc5983ma_suite, test_convert_raw_bytes_max_positive)
{
	struct mmc5983ma_raw_data data;

	/* Max value = 0x3FFFF = 262143
	 * Split: high 8 = 0xFF, mid 8 = 0xFF, low 2 = 0x03
	 */
	uint8_t raw_bytes[7] = {
		0xFF, 0xFF, /* X */
		0x80, 0x00, /* Y at midpoint */
		0x80, 0x00, /* Z at midpoint */
		0xC0        /* X low bits = 11, Y = 00, Z = 00 */
	};

	mmc5983ma_convert_raw_bytes(raw_bytes, &data);

	zassert_equal(data.x, 131071, "Max positive X should be 131071, got %d", data.x);
	zassert_equal(data.y, 0, "Y should be 0, got %d", data.y);
	zassert_equal(data.z, 0, "Z should be 0, got %d", data.z);
}

/**
 * @brief Test conversion of maximum negative field
 *
 * Minimum 18-bit value = 0
 * After offset: 0 - 131072 = -131072
 */
ZTEST(mmc5983ma_suite, test_convert_raw_bytes_max_negative)
{
	struct mmc5983ma_raw_data data;

	/* Min value = 0x00000 = 0 */
	uint8_t raw_bytes[7] = {
		0x00, 0x00, /* X */
		0x80, 0x00, /* Y at midpoint */
		0x80, 0x00, /* Z at midpoint */
		0x00        /* All low bits zero */
	};

	mmc5983ma_convert_raw_bytes(raw_bytes, &data);

	zassert_equal(data.x, -131072, "Max negative X should be -131072, got %d", data.x);
	zassert_equal(data.y, 0, "Y should be 0, got %d", data.y);
	zassert_equal(data.z, 0, "Z should be 0, got %d", data.z);
}

/**
 * @brief Test conversion with all axes having different values
 */
ZTEST(mmc5983ma_suite, test_convert_raw_bytes_mixed_values)
{
	struct mmc5983ma_raw_data data;

	/* X = 0x20400 = 132096 -> 132096 - 131072 = 1024
	 * Y = 0x1FC00 = 130048 -> 130048 - 131072 = -1024
	 * Z = 0x20000 = 131072 -> 0
	 */
	uint8_t raw_bytes[7] = {
		0x81, 0x00, /* X: 0x81 << 10 | 0x00 << 2 = 132096 */
		0x7F, 0x00, /* Y: 0x7F << 10 | 0x00 << 2 = 130048 */
		0x80, 0x00, /* Z: midpoint */
		0x00        /* Low bits all zero */
	};

	mmc5983ma_convert_raw_bytes(raw_bytes, &data);

	zassert_equal(data.x, 1024, "X should be 1024, got %d", data.x);
	zassert_equal(data.y, -1024, "Y should be -1024, got %d", data.y);
	zassert_equal(data.z, 0, "Z should be 0, got %d", data.z);
}

/* =============================================================================
 * Gauss Conversion Tests
 * =============================================================================
 */

/**
 * @brief Test conversion to Gauss with zero field
 */
ZTEST(mmc5983ma_suite, test_convert_to_gauss_zero)
{
	struct mmc5983ma_raw_data raw = {0, 0, 0};
	struct mmc5983ma_data data;

	mmc5983ma_convert_to_gauss(&raw, &data);

	zassert_true(fabsf(data.x) < 0.0001f, "X Gauss should be ~0");
	zassert_true(fabsf(data.y) < 0.0001f, "Y Gauss should be ~0");
	zassert_true(fabsf(data.z) < 0.0001f, "Z Gauss should be ~0");
	zassert_true(fabsf(data.magnitude) < 0.0001f, "Magnitude should be ~0");
}

/**
 * @brief Test conversion to Gauss with known values
 *
 * 16000 counts * 0.0000625 G/count = 1.0 Gauss
 */
ZTEST(mmc5983ma_suite, test_convert_to_gauss_one_gauss)
{
	struct mmc5983ma_raw_data raw = {16000, 0, 0};
	struct mmc5983ma_data data;

	mmc5983ma_convert_to_gauss(&raw, &data);

	zassert_true(fabsf(data.x - 1.0f) < 0.001f,
		     "16000 counts should be ~1.0 Gauss, got %f", (double)data.x);
	zassert_true(fabsf(data.magnitude - 1.0f) < 0.001f,
		     "Magnitude should be ~1.0 Gauss");
}

/**
 * @brief Test magnitude calculation with 3D vector
 *
 * Vector (3, 4, 0) should have magnitude 5
 * In counts: (48000, 64000, 0) -> Gauss (3, 4, 0) -> magnitude 5
 */
ZTEST(mmc5983ma_suite, test_convert_to_gauss_magnitude_3d)
{
	/* 48000 * 0.0000625 = 3.0 Gauss */
	/* 64000 * 0.0000625 = 4.0 Gauss */
	struct mmc5983ma_raw_data raw = {48000, 64000, 0};
	struct mmc5983ma_data data;

	mmc5983ma_convert_to_gauss(&raw, &data);

	zassert_true(fabsf(data.x - 3.0f) < 0.001f, "X should be 3.0 Gauss");
	zassert_true(fabsf(data.y - 4.0f) < 0.001f, "Y should be 4.0 Gauss");
	zassert_true(fabsf(data.magnitude - 5.0f) < 0.001f,
		     "Magnitude should be 5.0 Gauss (3-4-5 triangle), got %f",
		     (double)data.magnitude);
}

/**
 * @brief Test negative field conversion
 */
ZTEST(mmc5983ma_suite, test_convert_to_gauss_negative)
{
	struct mmc5983ma_raw_data raw = {-16000, -16000, -16000};
	struct mmc5983ma_data data;

	mmc5983ma_convert_to_gauss(&raw, &data);

	zassert_true(fabsf(data.x + 1.0f) < 0.001f, "X should be -1.0 Gauss");
	zassert_true(fabsf(data.y + 1.0f) < 0.001f, "Y should be -1.0 Gauss");
	zassert_true(fabsf(data.z + 1.0f) < 0.001f, "Z should be -1.0 Gauss");

	/* Magnitude of (-1, -1, -1) = sqrt(3) ≈ 1.732 */
	float expected_mag = sqrtf(3.0f);

	zassert_true(fabsf(data.magnitude - expected_mag) < 0.001f,
		     "Magnitude should be sqrt(3), got %f", (double)data.magnitude);
}

/* =============================================================================
 * Magnitude Calculation Tests
 * =============================================================================
 */

/**
 * @brief Test magnitude calculation directly
 */
ZTEST(mmc5983ma_suite, test_calculate_magnitude)
{
	float mag;

	/* Simple case: (1, 0, 0) -> 1 */
	mag = mmc5983ma_calculate_magnitude(1.0f, 0.0f, 0.0f);
	zassert_true(fabsf(mag - 1.0f) < 0.0001f, "Magnitude of (1,0,0) should be 1");

	/* 3-4-5 triangle: (3, 4, 0) -> 5 */
	mag = mmc5983ma_calculate_magnitude(3.0f, 4.0f, 0.0f);
	zassert_true(fabsf(mag - 5.0f) < 0.0001f, "Magnitude of (3,4,0) should be 5");

	/* 3D case: (1, 1, 1) -> sqrt(3) */
	mag = mmc5983ma_calculate_magnitude(1.0f, 1.0f, 1.0f);
	zassert_true(fabsf(mag - sqrtf(3.0f)) < 0.0001f,
		     "Magnitude of (1,1,1) should be sqrt(3)");

	/* Zero vector */
	mag = mmc5983ma_calculate_magnitude(0.0f, 0.0f, 0.0f);
	zassert_true(fabsf(mag) < 0.0001f, "Magnitude of (0,0,0) should be 0");
}

/* =============================================================================
 * Earth's Magnetic Field Range Test
 * =============================================================================
 */

/**
 * @brief Test that typical Earth field values are in expected range
 *
 * Earth's magnetic field is typically 0.25-0.65 Gauss
 * This test verifies our conversion produces reasonable values
 */
ZTEST(mmc5983ma_suite, test_earth_field_range)
{
	struct mmc5983ma_raw_data raw;
	struct mmc5983ma_data data;

	/* Simulate ~0.5 Gauss field (typical Earth field)
	 * 0.5 Gauss / 0.0000625 = 8000 counts
	 */
	raw.x = 5000;  /* ~0.31 G */
	raw.y = 4000;  /* ~0.25 G */
	raw.z = -6000; /* ~-0.38 G */

	mmc5983ma_convert_to_gauss(&raw, &data);

	/* Verify magnitude is in Earth field range (0.25 - 0.65 G) */
	zassert_true(data.magnitude > 0.2f && data.magnitude < 0.7f,
		     "Magnitude %f should be in Earth field range (0.2-0.7 G)",
		     (double)data.magnitude);
}

/* =============================================================================
 * Register Test Suite
 * =============================================================================
 */

ZTEST_SUITE(mmc5983ma_suite, NULL, mmc5983ma_suite_setup, NULL, NULL, NULL);
