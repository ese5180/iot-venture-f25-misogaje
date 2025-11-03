#include "mqtt.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mqtt, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <string.h>
#include <errno.h>

static struct mqtt_client client;
static struct sockaddr_storage broker;

/* -------------------- MQTT helper functions -------------------- */
static void broker_init(void)
{
    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(CONFIG_MQTT_BROKER_PORT);

    int ret = inet_pton(AF_INET, CONFIG_MQTT_BROKER_ADDR, &broker4->sin_addr);
    if (ret != 1)
    {
        LOG_ERR("inet_pton failed: %d", ret);
    }

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &broker4->sin_addr, addr_str, sizeof(addr_str));
    LOG_INF("Broker configured: %s:%d (parsed to: %s)", CONFIG_MQTT_BROKER_ADDR, CONFIG_MQTT_BROKER_PORT, addr_str);
}

static void mqtt_evt_handler(struct mqtt_client *const c,
                             const struct mqtt_evt *evt)
{
    switch (evt->type)
    {
    case MQTT_EVT_CONNACK:
        if (evt->result == 0)
        {
            LOG_INF("MQTT connected");
            struct mqtt_topic sub_topic = {
                .topic = {
                    .utf8 = (uint8_t *)CONFIG_MQTT_SUB_TOPIC,
                    .size = strlen(CONFIG_MQTT_SUB_TOPIC)},
                .qos = MQTT_QOS_1_AT_LEAST_ONCE};
            struct mqtt_subscription_list subs = {
                .list = &sub_topic,
                .list_count = 1,
                .message_id = 1};
            mqtt_subscribe(c, &subs);
            LOG_INF("Subscribed to %s", CONFIG_MQTT_SUB_TOPIC);
        }
        else
        {
            LOG_ERR("MQTT connect failed (%d)", evt->result);
        }
        break;

    case MQTT_EVT_PUBLISH:
        LOG_INF("Received message on %s", evt->param.publish.message.topic.topic.utf8);
        break;

    case MQTT_EVT_DISCONNECT:
        LOG_INF("MQTT disconnected (%d)", evt->result);
        break;

    default:
        break;
    }
}

/* -------------------- Public API -------------------- */
int mqtt_connect_broker(void)
{
    broker_init();

    mqtt_client_init(&client);
    client.broker = &broker;
    client.evt_cb = mqtt_evt_handler;
    client.client_id.utf8 = (uint8_t *)CONFIG_MQTT_CLIENT_ID;
    client.client_id.size = strlen(CONFIG_MQTT_CLIENT_ID);
    client.password = NULL;
    client.user_name = NULL;
    client.protocol_version = MQTT_VERSION_3_1_1;
    client.transport.type = MQTT_TRANSPORT_NON_SECURE;

    /* Static buffers to prevent heap allocation */
    static uint8_t mqtt_rx_buffer[2048];
    static uint8_t mqtt_tx_buffer[2048];
    client.rx_buf = mqtt_rx_buffer;
    client.rx_buf_size = sizeof(mqtt_rx_buffer);
    client.tx_buf = mqtt_tx_buffer;
    client.tx_buf_size = sizeof(mqtt_tx_buffer);

    LOG_INF("Connecting to MQTT broker...");
    int ret = mqtt_connect(&client);
    if (ret)
    {
        LOG_ERR("MQTT connect failed: %d (errno: %d)", ret, errno);
        return ret;
    }

    LOG_INF("mqtt_connect() succeeded, socket fd: %d", client.transport.tcp.sock);

    /* Set up poll to wait for connection response */
    struct pollfd fds[1];
    fds[0].fd = client.transport.tcp.sock;
    fds[0].events = POLLIN;

    /* Wait for CONNACK with timeout (5 seconds) */
    LOG_INF("Waiting for CONNACK...");
    ret = poll(fds, 1, 5000);
    if (ret < 0)
    {
        LOG_ERR("poll error: %d", errno);
        mqtt_abort(&client);
        return -errno;
    }
    else if (ret == 0)
    {
        LOG_ERR("Connection timeout - no CONNACK received");
        mqtt_abort(&client);
        return -ETIMEDOUT;
    }

    /* Process the CONNACK response */
    ret = mqtt_input(&client);
    if (ret != 0)
    {
        LOG_ERR("mqtt_input failed: %d", ret);
        mqtt_abort(&client);
        return ret;
    }

    LOG_INF("MQTT connection established");
    return 0;
}

int mqtt_publish_message(const char *topic, const char *payload)
{
    struct mqtt_publish_param param;
    param.message.topic.topic.utf8 = (uint8_t *)topic;
    param.message.topic.topic.size = strlen(topic);
    param.message.payload.data = (uint8_t *)payload;
    param.message.payload.len = strlen(payload);
    param.message_id = 1234;
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.dup_flag = 0U;
    param.retain_flag = 0U;

    int ret = mqtt_publish(&client, &param);
    if (ret == 0)
    {
        LOG_INF("Published message to %s", topic);
    }
    else
    {
        LOG_ERR("Failed to publish: %d", ret);
    }

    return ret;
}

int mqtt_run_loop(void)
{
    struct pollfd fds[1];
    fds[0].fd = client.transport.tcp.sock;
    fds[0].events = POLLIN;

    while (1)
    {
        /* Poll for incoming data with 1 second timeout */
        int ret = poll(fds, 1, 1000);
        if (ret > 0)
        {
            /* Data available, process it */
            ret = mqtt_input(&client);
            if (ret != 0)
            {
                LOG_ERR("mqtt_input error: %d", ret);
                return ret;
            }
        }
        else if (ret < 0)
        {
            LOG_ERR("poll error: %d", errno);
            return -errno;
        }

        /* Send keep-alive ping if needed */
        ret = mqtt_live(&client);
        if (ret != 0 && ret != -EAGAIN)
        {
            LOG_ERR("mqtt_live error: %d", ret);
            return ret;
        }

        k_sleep(K_MSEC(100));
    }
}

void mqtt_disconnect_broker(void)
{
    LOG_INF("Disconnecting from MQTT broker");
    mqtt_disconnect(&client, NULL);
}
