/*
 * Memfault Integration for MagNav/Misogate
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MEMFAULT_INTEGRATION_H
#define MEMFAULT_INTEGRATION_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the Memfault SDK
 * 
 * Call this once at application startup after hardware is ready.
 * 
 * @return 0 on success, negative errno on failure
 */
int memfault_integration_init(void);

/**
 * @brief Update magnetometer metrics for Memfault
 * 
 * Call this periodically with magnetometer readings. Values will be
 * included in the next heartbeat upload.
 * 
 * @param x_gauss Magnetic field X-axis in Gauss
 * @param y_gauss Magnetic field Y-axis in Gauss  
 * @param z_gauss Magnetic field Z-axis in Gauss
 * @param magnitude_gauss Total magnetic field magnitude in Gauss
 */
void memfault_update_mag_metrics(float x_gauss, float y_gauss, float z_gauss,
                                  float magnitude_gauss);

/**
 * @brief Record a magnetometer read error
 * 
 * Call this when a magnetometer read fails. This increments the error
 * counter and may trigger a trace event.
 */
void memfault_record_mag_error(void);

/**
 * @brief Update WiFi connection state
 * 
 * @param connected true if WiFi is connected, false otherwise
 */
void memfault_set_wifi_connected(bool connected);

/**
 * @brief Update MQTT/AWS IoT connection state
 * 
 * @param connected true if MQTT is connected, false otherwise
 */
void memfault_set_mqtt_connected(bool connected);

/**
 * @brief Upload any pending Memfault data
 * 
 * This sends coredumps, metrics, and traces to the Memfault cloud.
 * Requires WiFi connectivity.
 * 
 * @return 0 on success, negative errno on failure
 */
int memfault_upload_data(void);

/**
 * @brief Check Memfault server for OTA updates
 * 
 * If an update is available, this will initiate the download.
 * Requires WiFi connectivity.
 * 
 * @return >0 if update initiated, 0 if no update, negative on error
 */
int memfault_check_for_ota(void);

/**
 * @brief Trigger a test crash for coredump testing
 * 
 * WARNING: This will crash the device! Use only for testing
 * that coredumps are being captured correctly.
 */
void memfault_test_trigger_coredump(void);

/**
 * @brief Trigger a test assert for coredump testing
 * 
 * WARNING: This will crash the device! Use only for testing.
 */
void memfault_test_assert(void);

#ifdef __cplusplus
}
#endif

#endif /* MEMFAULT_INTEGRATION_H */
