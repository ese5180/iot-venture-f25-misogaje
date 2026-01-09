#include "mqtt.h"
#include <string.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_REGISTER(mqtt, CONFIG_MISOGATE_LOG_LEVEL);

int mqtt_init(void) {
  int err;
  static const struct mqtt_topic topic_list[] = {{
      .topic.utf8 = MISOGATE_SUB,
      .topic.size = strlen(MISOGATE_SUB),
      .qos = MQTT_QOS_1_AT_LEAST_ONCE,
  }};

  LOG_INF("Initializing MQTT application topics");

  err = aws_iot_application_topics_set(topic_list, ARRAY_SIZE(topic_list));
  if (err) {
    LOG_ERR("aws_iot_application_topics_set, error: %d", err);
    return err;
  }

  LOG_INF("Subscribed to topic: %s", MISOGATE_SUB);

  return 0;
}

int mqtt_publish_json(const char *json_message, size_t len, enum mqtt_qos qos) {
  int err;
  struct aws_iot_data tx_data = {
      .qos = qos,
      .topic.type = AWS_IOT_SHADOW_TOPIC_NONE,
      .topic.str = MISOGATE_PUB,
      .topic.len = strlen(MISOGATE_PUB),
      .ptr = json_message,
      .len = len,
  };

  if (!json_message || len == 0) {
    LOG_ERR("Invalid JSON message or length");
    return -EINVAL;
  }

  LOG_INF("Publishing JSON message to %s: %.*s", MISOGATE_PUB, (int)len,
          json_message);

  err = aws_iot_send(&tx_data);
  if (err) {
    LOG_ERR("Failed to publish to %s, error: %d", MISOGATE_PUB, err);
    return err;
  }

  LOG_INF("Successfully published to %s", MISOGATE_PUB);

  return 0;
}

void mqtt_handle_received_data(const struct aws_iot_evt *const evt) {
  if (!evt || !evt->data.msg.ptr) {
    LOG_ERR("Invalid event data");
    return;
  }

  /* Check if the message is on MISOGATE_SUB topic and log it */
  if (evt->data.msg.topic.len == strlen(MISOGATE_SUB) &&
      strncmp(evt->data.msg.topic.str, MISOGATE_SUB, strlen(MISOGATE_SUB)) ==
          0) {
    LOG_INF("Received on %s: \"%.*s\"", MISOGATE_SUB, evt->data.msg.len,
            evt->data.msg.ptr);
  } else {
    /* Log messages on other topics */
    LOG_INF("Received on topic \"%.*s\": \"%.*s\"", evt->data.msg.topic.len,
            evt->data.msg.topic.str, evt->data.msg.len, evt->data.msg.ptr);
  }
}
