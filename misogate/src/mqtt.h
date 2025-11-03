#ifndef MQTT_H
#define MQTT_H

#include <zephyr/kernel.h>

/**
 * @brief Initialize and connect to MQTT broker
 * @return 0 on success, negative errno on failure
 */
int mqtt_connect_broker(void);

/**
 * @brief Publish a message to a topic
 * @param topic Topic string to publish to
 * @param payload Payload string to publish
 * @return 0 on success, negative errno on failure
 */
int mqtt_publish_message(const char *topic, const char *payload);

/**
 * @brief Main MQTT processing loop
 *
 * This function runs indefinitely and handles:
 * - Polling for incoming MQTT messages
 * - Processing received messages
 * - Sending keep-alive pings
 *
 * @return Does not return normally. Returns negative errno on error.
 */
int mqtt_run_loop(void);

/**
 * @brief Disconnect from MQTT broker
 */
void mqtt_disconnect_broker(void);

#endif /* MQTT_H */
