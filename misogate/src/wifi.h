#ifndef WIFI_H
#define WIFI_H

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * @brief Initialize WiFi subsystem and register event callbacks
 */
void wifi_init(void);

/**
 * @brief Connect to WiFi using stored credentials
 * @return 0 on success, negative errno on failure
 */
int wifi_connect(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Check if DHCP has bound an IP address
 * @return true if DHCP bound, false otherwise
 */
bool wifi_is_dhcp_bound(void);

/**
 * @brief Wait for WiFi connection to complete
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return 0 on success, -ETIMEDOUT on timeout, negative errno on failure
 */
int wifi_wait_for_connection(int timeout_ms);

/**
 * @brief Wait for DHCP to complete
 * @param timeout_ms Maximum time to wait in milliseconds
 * @return 0 on success, -ETIMEDOUT on timeout
 */
int wifi_wait_for_dhcp(int timeout_ms);

/**
 * @brief Print WiFi MAC address
 */
void wifi_print_mac_address(void);

#endif /* WIFI_H */
