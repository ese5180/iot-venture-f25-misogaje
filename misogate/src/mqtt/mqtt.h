#ifndef MQTT_H
#define MQTT_H

#include <net/aws_iot.h>

/**
 * @brief MQTT topic definitions
 */
#define MISOGATE_PUB "misogate/pub"
#define MISOGATE_SUB "misogate/sub"

/**
 * @brief Initialize MQTT application topics (subscribe to MISOGATE_SUB)
 * 
 * @return 0 on success, negative errno on failure
 */
int mqtt_init(void);

/**
 * @brief Publish a JSON message to MISOGATE_PUB topic
 * 
 * @param json_message Pointer to the JSON string to publish
 * @param len Length of the JSON message
 * @param qos Quality of Service level (MQTT_QOS_0_AT_MOST_ONCE, MQTT_QOS_1_AT_LEAST_ONCE, etc.)
 * 
 * @return 0 on success, negative errno on failure
 */
int mqtt_publish_json(const char *json_message, size_t len, enum mqtt_qos qos);

/**
 * @brief Handle received MQTT data on subscribed topics
 * 
 * @param evt AWS IoT event containing the received message
 */
void mqtt_handle_received_data(const struct aws_iot_evt *const evt);

#endif /* MQTT_H */

