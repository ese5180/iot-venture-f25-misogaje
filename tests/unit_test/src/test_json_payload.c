/*
 * =============================================================================
 * JSON Payload Unit Tests
 * =============================================================================
 * Tests for the misogate JSON payload construction functionality.
 * 
 * REPLACE THIS with your actual TDD tests from earlier in the course!
 * =============================================================================
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * Test Suite Setup/Teardown
 * =============================================================================
 */

static void *json_payload_suite_setup(void)
{
	/* Setup code that runs once before all tests in this suite */
	printk("Setting up JSON payload test suite\n");
	return NULL;
}

static void json_payload_before(void *fixture)
{
	/* Runs before each test */
	ARG_UNUSED(fixture);
}

static void json_payload_after(void *fixture)
{
	/* Runs after each test */
	ARG_UNUSED(fixture);
}

/* =============================================================================
 * Test Cases
 * =============================================================================
 */

/**
 * @brief Test that a simple JSON message can be formatted correctly
 */
ZTEST(json_payload_suite, test_json_format_basic)
{
	char buffer[128];
	int counter = 42;
	
	int ret = snprintf(buffer, sizeof(buffer), "{\"position\": %d}", counter);
	
	zassert_true(ret > 0, "snprintf should return positive value");
	zassert_true(ret < sizeof(buffer), "Buffer should not overflow");
	zassert_true(strstr(buffer, "\"position\"") != NULL, 
		     "JSON should contain position key");
	zassert_true(strstr(buffer, "42") != NULL,
		     "JSON should contain counter value");
}

/**
 * @brief Test JSON format with zero value
 */
ZTEST(json_payload_suite, test_json_format_zero)
{
	char buffer[128];
	int counter = 0;
	
	int ret = snprintf(buffer, sizeof(buffer), "{\"position\": %d}", counter);
	
	zassert_true(ret > 0, "snprintf should succeed");
	zassert_mem_equal(buffer, "{\"position\": 0}", strlen("{\"position\": 0}"),
			  "JSON should match expected format");
}

/**
 * @brief Test JSON format with maximum counter value
 */
ZTEST(json_payload_suite, test_json_format_max_value)
{
	char buffer[128];
	int counter = 255;  /* Max counter value per button_handler logic */
	
	int ret = snprintf(buffer, sizeof(buffer), "{\"position\": %d}", counter);
	
	zassert_true(ret > 0, "snprintf should succeed");
	zassert_true(strstr(buffer, "255") != NULL,
		     "JSON should contain max counter value");
}

/**
 * @brief Test buffer overflow protection
 */
ZTEST(json_payload_suite, test_json_buffer_too_small)
{
	char small_buffer[10];  /* Too small for the JSON */
	int counter = 12345;
	
	int ret = snprintf(small_buffer, sizeof(small_buffer), 
			   "{\"position\": %d}", counter);
	
	/* snprintf should return what WOULD have been written */
	zassert_true(ret >= sizeof(small_buffer),
		     "Return value should indicate truncation");
	
	/* Buffer should be null-terminated even if truncated */
	zassert_equal(small_buffer[sizeof(small_buffer) - 1], '\0',
		      "Buffer should be null-terminated");
}

/**
 * @brief Test NABC-format telemetry JSON structure
 * 
 * This tests the JSON format required by NABC competition:
 * { "team": ..., "timestamp": ..., "chainage": ..., etc. }
 */
ZTEST(json_payload_suite, test_nabc_telemetry_format)
{
	char buffer[512];
	const char *team_name = "misogaje";
	uint32_t timestamp = 1234567890;
	bool mining = true;
	float chainage = 10.5f;
	float easting = 100.25f;
	float northing = 200.75f;
	float elevation = -5.0f;
	float heading = 1.57f;  /* ~90 degrees in radians */
	
	int ret = snprintf(buffer, sizeof(buffer),
		"{"
		"\"team\": \"%s\", "
		"\"timestamp\": %u, "
		"\"mining\": %s, "
		"\"chainage\": %.2f, "
		"\"easting\": %.2f, "
		"\"northing\": %.2f, "
		"\"elevation\": %.2f, "
		"\"roll\": %.2f, "
		"\"pitch\": %.2f, "
		"\"heading\": %.2f"
		"}",
		team_name, timestamp, mining ? "true" : "false",
		chainage, easting, northing, elevation,
		0.0f, 0.0f, heading);
	
	zassert_true(ret > 0 && ret < sizeof(buffer),
		     "JSON formatting should succeed");
	
	/* Verify required fields are present */
	zassert_not_null(strstr(buffer, "\"team\""), "Should have team field");
	zassert_not_null(strstr(buffer, "\"timestamp\""), "Should have timestamp field");
	zassert_not_null(strstr(buffer, "\"mining\""), "Should have mining field");
	zassert_not_null(strstr(buffer, "\"chainage\""), "Should have chainage field");
	zassert_not_null(strstr(buffer, "\"heading\""), "Should have heading field");
	zassert_not_null(strstr(buffer, "misogaje"), "Should have team name");
}

/* =============================================================================
 * Register Test Suite
 * =============================================================================
 */

ZTEST_SUITE(json_payload_suite, NULL, json_payload_suite_setup, 
	    json_payload_before, json_payload_after, NULL);
