#include "mqtt.h"
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/random/random.h>
#include <stdio.h>

LOG_MODULE_REGISTER(mqtt, CONFIG_MISOGATE_LOG_LEVEL);

#define SERVER_HOST CONFIG_MISOGATE_MQTT_BROKER_HOSTNAME
#define SERVER_PORT CONFIG_MISOGATE_MQTT_BROKER_PORT
#define MQTT_CLIENTID CONFIG_MISOGATE_MQTT_CLIENT_ID
#define MQTT_USERNAME CONFIG_MISOGATE_MQTT_USERNAME
#define MQTT_PASSWORD CONFIG_MISOGATE_MQTT_PASSWORD

/* Buffers for MQTT client. */
static uint8_t rx_buffer[1024];
static uint8_t tx_buffer[1024];

/* The mqtt client struct */
static struct mqtt_client client_ctx;

/* MQTT Broker details. */
static struct sockaddr_storage broker;

/* Connected flag */
static bool connected = false;
static bool connecting = false;

/* File descriptor */
static struct pollfd fds[1];

static struct mqtt_utf8 username;
static struct mqtt_utf8 password;

#if defined(CONFIG_MQTT_LIB_TLS)
#include <zephyr/net/tls_credentials.h>
#define TLS_SEC_TAG 42
#endif

static void prepare_fds(struct mqtt_client *client)
{
    if (client->transport.type == MQTT_TRANSPORT_NON_SECURE)
    {
        fds[0].fd = client->transport.tcp.sock;
    }
#if defined(CONFIG_MQTT_LIB_TLS)
    else if (client->transport.type == MQTT_TRANSPORT_SECURE)
    {
        fds[0].fd = client->transport.tls.sock;
    }
#endif

    fds[0].events = POLLIN;
    fds[0].revents = 0;
}

static void clear_fds(void)
{
    fds[0].fd = -1;
}

static void wait(int timeout)
{
    if (poll(fds, 1, timeout) < 0)
    {
        LOG_ERR("poll error: %d", errno);
    }
}

void mqtt_evt_handler(struct mqtt_client *const client,
                      const struct mqtt_evt *evt)
{
    int err;

    switch (evt->type)
    {
    case MQTT_EVT_CONNACK:
        if (evt->result != 0)
        {
            LOG_ERR("MQTT connect failed with result code: %d", evt->result);
            connected = false;
            connecting = false;
            break;
        }

        connected = true;
        connecting = false;
        LOG_INF("MQTT client connected successfully!");

        // Subscribe to topic
        struct mqtt_topic subscribe_topic = {
            .topic = {
                .utf8 = MISOGATE_SUB,
                .size = strlen(MISOGATE_SUB)},
            .qos = MQTT_QOS_1_AT_LEAST_ONCE};

        const struct mqtt_subscription_list subscription_list = {
            .list = &subscribe_topic,
            .list_count = 1,
            .message_id = 1 // Randomize?
        };

        err = mqtt_subscribe(client, &subscription_list);
        if (err)
        {
            LOG_ERR("Failed to subscribe to %s, error: %d", MISOGATE_SUB, err);
        }
        else
        {
            LOG_INF("Subscribed to %s", MISOGATE_SUB);
        }

        break;

    case MQTT_EVT_DISCONNECT:
        LOG_INF("MQTT client disconnected %d", evt->result);
        connected = false;
        connecting = false;
        clear_fds();
        break;

    case MQTT_EVT_PUBLISH:
    {
        const struct mqtt_publish_param *p = &evt->param.publish;

        LOG_INF("MQTT PUBLISH result=%d len=%d", evt->result, p->message.payload.len);
        if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE)
        {
            struct mqtt_puback_param puback = {
                .message_id = p->message_id};
            mqtt_publish_qos1_ack(client, &puback);
        }
        // Handle payload (log it for now)
        if (p->message.payload.len > 0)
        {
            // In a real app we might need to buffer this if it's fragmented
            // For now assume small messages
            LOG_INF("Received on topic \"%.*s\"",
                    p->message.topic.topic.size,
                    p->message.topic.topic.utf8);
        }
        break;
    }

    case MQTT_EVT_PUBACK:
        if (evt->result != 0)
        {
            LOG_ERR("MQTT PUBACK error %d", evt->result);
            break;
        }
        LOG_INF("PUBACK packet id: %u", evt->param.puback.message_id);
        break;

    default:
        LOG_DBG("Unhandled MQTT event type: %d", evt->type);
        break;
    }
}

static int broker_init(void)
{
    int err;
    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

    // Use DNS lookup since we have a hostname
    struct zsock_addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    char port_str[6];
    sprintf(port_str, "%d", SERVER_PORT);

    err = zsock_getaddrinfo(SERVER_HOST, port_str, &hints, &res);
    if (err)
    {
        LOG_ERR("getaddrinfo failed: %d", err);
        return err;
    }

    if (res->ai_family == AF_INET)
    {
        memcpy(broker4, res->ai_addr, sizeof(struct sockaddr_in));
    }
    else
    {
        LOG_ERR("Not IPv4");
        zsock_freeaddrinfo(res);
        return -EINVAL;
    }

    zsock_freeaddrinfo(res);
    return 0;
}

static void client_init(struct mqtt_client *client)
{
    mqtt_client_init(client);

    broker_init();

    /* MQTT client configuration */
    client->broker = &broker;
    client->evt_cb = mqtt_evt_handler;
    client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
    client->client_id.size = strlen(MQTT_CLIENTID);
    client->password = &password;
    client->user_name = &username;
    client->protocol_version = MQTT_VERSION_3_1_1;

    client->rx_buf = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);
    client->tx_buf = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);

    username.utf8 = (uint8_t *)MQTT_USERNAME;
    username.size = strlen(MQTT_USERNAME);
    password.utf8 = (uint8_t *)MQTT_PASSWORD;
    password.size = strlen(MQTT_PASSWORD);

#if defined(CONFIG_MQTT_LIB_TLS)
    client->transport.type = MQTT_TRANSPORT_SECURE;
    client->transport.tls.config.peer_verify = TLS_PEER_VERIFY_OPTIONAL; // Relaxed for now, strictly should be REQUIRED
    client->transport.tls.config.cipher_list = NULL;
    client->transport.tls.config.sec_tag_list = NULL; // We'll add this if we have certs
    client->transport.tls.config.sec_tag_count = 0;
    client->transport.tls.config.hostname = SERVER_HOST;
#else
    client->transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif
}

int mqtt_app_init(void)
{
    client_init(&client_ctx);
    return 0;
}

int mqtt_app_connect(void)
{
    int err;

    if (connected)
    {
        LOG_INF("Already connected to MQTT broker");
        return 0;
    }

    if (connecting)
    {
        LOG_DBG("Connection already in progress");
        return -EALREADY;
    }

    LOG_INF("Resolving broker hostname: %s", SERVER_HOST);
    err = broker_init(); // Re-resolve in case IP changed
    if (err)
    {
        LOG_ERR("Failed to resolve broker hostname, error: %d", err);
        return err;
    }

    LOG_INF("Initiating MQTT connection to %s:%d", SERVER_HOST, SERVER_PORT);
    LOG_INF("Client ID: %s, Username: %s", MQTT_CLIENTID, MQTT_USERNAME);

    connecting = true;
    err = mqtt_connect(&client_ctx);
    if (err)
    {
        LOG_ERR("mqtt_connect failed: %d", err);
        connecting = false;
        return err;
    }

    prepare_fds(&client_ctx);
    LOG_INF("MQTT connection initiated, waiting for CONNACK...");
    return 0;
}

void mqtt_app_disconnect(void)
{
    if (connected || connecting)
    {
        LOG_INF("Disconnecting from MQTT broker");
        mqtt_disconnect(&client_ctx, NULL);
        connected = false;
        connecting = false;
    }
}

void mqtt_app_input(void)
{
    // Process MQTT events regardless of connection state
    // This is necessary to receive CONNACK and complete the handshake
    if (connecting || connected)
    {
        wait(10); // Short wait
        mqtt_input(&client_ctx);

        // Handle keepalive
        if (connected)
        {
            mqtt_live(&client_ctx);
        }
    }
}

int mqtt_publish_json(const char *json_message, size_t len, enum mqtt_qos qos)
{
    struct mqtt_publish_param param;

    if (!connected)
    {
        LOG_WRN("Cannot publish: MQTT not connected (connecting=%d)", connecting);
        return -ENOTCONN;
    }

    param.message.topic.qos = qos;
    param.message.topic.topic.utf8 = (uint8_t *)MISOGATE_PUB;
    param.message.topic.topic.size = strlen(MISOGATE_PUB);
    param.message.payload.data = (uint8_t *)json_message;
    param.message.payload.len = len;
    param.message_id = sys_rand32_get();
    param.dup_flag = 0;
    param.retain_flag = 0;

    LOG_DBG("Publishing %d bytes to %s", len, MISOGATE_PUB);
    return mqtt_publish(&client_ctx, &param);
}
