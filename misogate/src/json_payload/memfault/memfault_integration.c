/*
 * Memfault Integration for MagNav/Misogate
 * SPDX-License-Identifier: Apache-2.0
 *
 * This module integrates Memfault SDK for:
 * - OTA firmware updates
 * - Core dumps
 * - Custom magnetometer metrics
 */

#include "memfault_integration.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <memfault/components.h>
#include <memfault/metrics/metrics.h>
#include <memfault/core/data_packetizer.h>
#include <memfault/http/http_client.h>
#include <memfault/core/trace_event.h>

LOG_MODULE_REGISTER(memfault_integration, LOG_LEVEL_INF);

/* State tracking */
static bool s_wifi_connected = false;
static bool s_mqtt_connected = false;
static uint32_t s_mag_read_success = 0;
static uint32_t s_mag_read_errors = 0;

/* Latest magnetometer values (scaled to 0.0001 Gauss) */
static int32_t s_mag_x = 0;
static int32_t s_mag_y = 0;
static int32_t s_mag_z = 0;
static uint32_t s_mag_magnitude = 0;

/* =============================================================================
 * Initialization
 * ============================================================================= */

int memfault_integration_init(void)
{
    LOG_INF("Initializing Memfault integration");
    
    /* Initialize the Memfault SDK */
    memfault_platform_boot();
    
    /* Log device info */
    sMemfaultDeviceInfo device_info;
    memfault_platform_get_device_info(&device_info);
    
    LOG_INF("Memfault Device ID: %s", device_info.device_serial);
    LOG_INF("Memfault FW Version: %s", device_info.software_version);
    LOG_INF("Memfault FW Type: %s", device_info.software_type);
    LOG_INF("Memfault HW Version: %s", device_info.hardware_version);
    
    return 0;
}

/* =============================================================================
 * Magnetometer Metrics
 * ============================================================================= */

void memfault_update_mag_metrics(float x_gauss, float y_gauss, float z_gauss, 
                                  float magnitude_gauss)
{
    /* Scale to 0.0001 Gauss units (integer representation) */
    s_mag_x = (int32_t)(x_gauss * 10000.0f);
    s_mag_y = (int32_t)(y_gauss * 10000.0f);
    s_mag_z = (int32_t)(z_gauss * 10000.0f);
    s_mag_magnitude = (uint32_t)(magnitude_gauss * 10000.0f);
    
    s_mag_read_success++;
    
    LOG_DBG("Mag metrics updated: X=%d, Y=%d, Z=%d, Mag=%u (x10000 Gauss)",
            s_mag_x, s_mag_y, s_mag_z, s_mag_magnitude);
}

void memfault_record_mag_error(void)
{
    s_mag_read_errors++;
    
    /* Record trace event for I2C errors */
    MEMFAULT_TRACE_EVENT_WITH_LOG(mag_i2c_error, 
                                  "Magnetometer read failed, total errors: %u",
                                  s_mag_read_errors);
}

/* =============================================================================
 * Connection State Tracking
 * ============================================================================= */

void memfault_set_wifi_connected(bool connected)
{
    if (s_wifi_connected && !connected) {
        /* WiFi just disconnected - record trace */
        MEMFAULT_TRACE_EVENT(wifi_disconnected);
    }
    s_wifi_connected = connected;
}

void memfault_set_mqtt_connected(bool connected)
{
    if (s_mqtt_connected && !connected) {
        /* MQTT just disconnected - record trace */
        MEMFAULT_TRACE_EVENT(mqtt_disconnected);
    }
    s_mqtt_connected = connected;
}

/* =============================================================================
 * Heartbeat Collection (called by Memfault SDK periodically)
 * ============================================================================= */

/*
 * This function is called by the Memfault SDK before sending a heartbeat.
 * We populate our custom metrics here.
 * 
 * Note: If using NCS >= 2.6.0, set CONFIG_MEMFAULT_NCS_IMPLEMENT_METRICS_COLLECTION=n
 * and call memfault_ncs_metrics_collect() here as well.
 */
void memfault_metrics_heartbeat_collect_data(void)
{
    /* Update magnetometer metrics */
    memfault_metrics_heartbeat_set_signed(MEMFAULT_METRICS_KEY(mag_x_gauss_x10000), 
                                          s_mag_x);
    memfault_metrics_heartbeat_set_signed(MEMFAULT_METRICS_KEY(mag_y_gauss_x10000), 
                                          s_mag_y);
    memfault_metrics_heartbeat_set_signed(MEMFAULT_METRICS_KEY(mag_z_gauss_x10000), 
                                          s_mag_z);
    memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(mag_magnitude_x10000), 
                                            s_mag_magnitude);
    
    /* Update read counts */
    memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(mag_read_success_count), 
                                            s_mag_read_success);
    memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(mag_read_error_count), 
                                            s_mag_read_errors);
    
    /* Connection states */
    memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(wifi_connected), 
                                            s_wifi_connected ? 1 : 0);
    memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(mqtt_connected), 
                                            s_mqtt_connected ? 1 : 0);
    
    /* Uptime */
    memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(uptime_seconds), 
                                            (uint32_t)(k_uptime_get() / 1000));
    
    /* Reset counters for next heartbeat period */
    s_mag_read_success = 0;
    s_mag_read_errors = 0;
    
    LOG_INF("Memfault heartbeat collected");
}

/* =============================================================================
 * Data Upload
 * ============================================================================= */

int memfault_upload_data(void)
{
    if (!s_wifi_connected) {
        LOG_WRN("Cannot upload Memfault data: WiFi not connected");
        return -ENOTCONN;
    }
    
    /* Check if there's data to send */
    if (!memfault_packetizer_data_available()) {
        LOG_DBG("No Memfault data to upload");
        return 0;
    }
    
    LOG_INF("Uploading Memfault data...");
    
    /* Upload data to Memfault cloud */
    int rv = memfault_zephyr_port_post_data();
    if (rv == 0) {
        LOG_INF("Memfault data uploaded successfully");
    } else {
        LOG_ERR("Memfault upload failed: %d", rv);
    }
    
    return rv;
}

/* =============================================================================
 * OTA/FOTA Support
 * ============================================================================= */

int memfault_check_for_ota(void)
{
    if (!s_wifi_connected) {
        LOG_WRN("Cannot check for OTA: WiFi not connected");
        return -ENOTCONN;
    }
    
    LOG_INF("Checking Memfault for OTA updates...");
    
    /* This will check the Memfault backend for available updates */
    int rv = memfault_zephyr_port_ota_update();
    
    if (rv > 0) {
        LOG_INF("OTA update available and initiated");
    } else if (rv == 0) {
        LOG_INF("No OTA update available");
    } else {
        LOG_ERR("OTA check failed: %d", rv);
    }
    
    return rv;
}

/* =============================================================================
 * Test/Debug Functions
 * ============================================================================= */

void memfault_test_trigger_coredump(void)
{
    LOG_WRN("Triggering test crash for coredump...");
    
    /* Give time for log to flush */
    k_msleep(100);
    
    /* Trigger a crash - this will be captured by Memfault */
    volatile int *bad_ptr = (int *)0xDEADBEEF;
    *bad_ptr = 42;  /* This will cause a fault */
}

void memfault_test_assert(void)
{
    LOG_WRN("Triggering test assert for coredump...");
    
    k_msleep(100);
    
    /* Use Memfault's assert macro */
    MEMFAULT_ASSERT(0);
}
