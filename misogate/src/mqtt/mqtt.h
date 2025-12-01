#ifndef MQTT_H
#define MQTT_H

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
 * @brief Connect to the MQTT broker
 *
 * @return 0 on success, negative errno on failure
 */
int mqtt_app_connect(void);

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
