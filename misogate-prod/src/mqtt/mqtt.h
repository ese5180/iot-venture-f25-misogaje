#ifndef MQTT_H
#define MQTT_H

#include <stdbool.h>
#include <zephyr/net/mqtt.h>

/**
 * @brief MQTT topic definitions
 */
#define MISOGATE_PUB "misogate/pub"
#define MISOGATE_SUB "misogate/sub"

/**
 * @brief Initialize MQTT client
 *
 * @return 0 on success, negative errno on failure
 */
int mqtt_app_init(void);

/**
 * @brief Connect to the MQTT broker (non-blocking, initiates connection)
 *
 * @return 0 on success, negative errno on failure
 */
int mqtt_app_connect(void);

/**
 * @brief Connect to the MQTT broker with retries (up to 3 attempts)
 *
 * @return 0 on success, negative errno on failure after all retries
 */
int mqtt_app_connect_with_retries(void);

/**
 * @brief Wait for MQTT connection to be fully established
 *
 * This function blocks until CONNACK is received or timeout expires.
 * Must be called after mqtt_app_connect() returns successfully.
 *
 * @param timeout_ms Maximum time to wait in milliseconds
 *
 * @return 0 if connected, -ETIMEDOUT if timeout, other negative errno on error
 */
int mqtt_wait_connected(uint32_t timeout_ms);

/**
 * @brief Check if MQTT is fully connected (CONNACK received)
 *
 * @return true if connected, false otherwise
 */
bool mqtt_is_connected(void);

/**
 * @brief Check if MQTT connection is in progress
 *
 * @return true if connecting, false otherwise
 */
bool mqtt_is_connecting(void);

/**
 * @brief Disconnect from the MQTT broker
 */
void mqtt_app_disconnect(void);

/**
 * @brief Process MQTT input (call this regularly or from a thread)
 */
void mqtt_app_input(void);

/**
 * @brief Publish a JSON message to MISOGATE_PUB topic
 *
 * @param json_message Pointer to the JSON string to publish
 * @param len Length of the JSON message
 * @param qos Quality of Service level
 *
 * @return 0 on success, negative errno on failure
 */
int mqtt_publish_json(const char *json_message, size_t len, enum mqtt_qos qos);

#endif /* MQTT_H */
