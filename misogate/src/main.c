

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback net_mgmt_cb;

static struct
{
    bool connected;
    bool connect_result;
    bool dhcp_bound;
} context;

static struct mqtt_client client;
static struct sockaddr_storage broker;

#define MQTT_BROKER_ADDR "54.36.178.49"
#define MQTT_BROKER_PORT 1883
#define MQTT_CLIENT_ID "misogate_device_019a0cb4"
#define MQTT_PUB_TOPIC "test/pub"
#define MQTT_SUB_TOPIC "test/sub"

/* -------------------- Wi-Fi event handling -------------------- */
static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;
    if (status->status)
    {
        LOG_ERR("Connection failed (%d)", status->status);
    }
    else
    {
        LOG_INF("Connected to WiFi");
        context.connected = true;
    }
    context.connect_result = true;
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;
    LOG_INF("Disconnected from WiFi (%d)", status->status);
    context.connected = false;
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event)
    {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        handle_wifi_connect_result(cb);
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        handle_wifi_disconnect_result(cb);
        break;
    default:
        break;
    }
}

/* -------------------- DHCP event handling -------------------- */
static void print_dhcp_ip(struct net_mgmt_event_callback *cb)
{
    const struct net_if_dhcpv4 *dhcpv4 = cb->info;
    const struct in_addr *addr = &dhcpv4->requested_ip;
    char dhcp_info[64];
    net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));
    LOG_INF("DHCP IP address: %s", dhcp_info);
}

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND)
    {
        print_dhcp_ip(cb);
        context.dhcp_bound = true;
    }
}

/* -------------------- Wi-Fi connection -------------------- */
static void print_mac_address(void)
{
    struct net_if *iface = net_if_get_first_wifi();
    if (iface == NULL)
    {
        LOG_ERR("No WiFi interface found");
        return;
    }

    struct net_linkaddr *link_addr = net_if_get_link_addr(iface);
    if (link_addr == NULL)
    {
        LOG_ERR("No link address found");
        return;
    }

    LOG_INF("WiFi MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
            link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
            link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
}

static int wifi_connect(void)
{
    struct net_if *iface = net_if_get_first_wifi();
    context.connected = false;
    context.connect_result = false;

    if (net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0))
    {
        LOG_ERR("Connection request failed");
        return -ENOEXEC;
    }
    LOG_INF("Connection requested");
    return 0;
}

/* -------------------- MQTT helper functions -------------------- */
static void broker_init(void)
{
    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
    broker4->sin_family = AF_INET;
    broker4->sin_port = htons(MQTT_BROKER_PORT);

    int ret = inet_pton(AF_INET, MQTT_BROKER_ADDR, &broker4->sin_addr);
    if (ret != 1)
    {
        LOG_ERR("inet_pton failed: %d", ret);
    }

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &broker4->sin_addr, addr_str, sizeof(addr_str));
    LOG_INF("Broker configured: %s:%d (parsed to: %s)", MQTT_BROKER_ADDR, MQTT_BROKER_PORT, addr_str);
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
                    .utf8 = (uint8_t *)MQTT_SUB_TOPIC,
                    .size = strlen(MQTT_SUB_TOPIC)},
                .qos = MQTT_QOS_1_AT_LEAST_ONCE};
            struct mqtt_subscription_list subs = {
                .list = &sub_topic,
                .list_count = 1,
                .message_id = 1};
            mqtt_subscribe(c, &subs);
            LOG_INF("Subscribed to %s", MQTT_SUB_TOPIC);
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

// static int mqtt_connect_broker(void)
// {
//     broker_init();
//     mqtt_client_init(&client);
//     client.broker = &broker;
//     client.evt_cb = mqtt_evt_handler;
//     client.client_id.utf8 = (uint8_t *)MQTT_CLIENT_ID;
//     client.client_id.size = strlen(MQTT_CLIENT_ID);
//     client.protocol_version = MQTT_VERSION_3_1_1;
//     client.transport.type = MQTT_TRANSPORT_NON_SECURE;

//     int ret = mqtt_connect(&client);
//     if (ret) {
//         LOG_ERR("MQTT connect failed: %d", ret);
//         return ret;
//     }
//     LOG_INF("Connecting to MQTT broker...");
//     return 0;
// }
static int mqtt_connect_broker(void)
{
    broker_init();

    mqtt_client_init(&client);
    client.broker = &broker;
    client.evt_cb = mqtt_evt_handler;
    client.client_id.utf8 = (uint8_t *)MQTT_CLIENT_ID;
    client.client_id.size = strlen(MQTT_CLIENT_ID);
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

/* -------------------- Main -------------------- */
int main(void)
{
    LOG_INF("Starting WiFi + MQTT demo");

    /* === Dynamic heap availability estimation === */
    size_t free_estimate = 0;
    size_t step = 1024; // Allocate in 1 KB chunks
    void *ptrs[64];
    int i = 0;

    for (i = 0; i < 64; i++)
    {
        ptrs[i] = k_malloc(step);
        if (ptrs[i] == NULL)
        {
            break;
        }
        free_estimate += step;
    }

    LOG_INF("Estimated free heap before MQTT: ~%zu bytes", free_estimate);

    for (int j = 0; j < i; j++)
    {
        k_free(ptrs[j]);
    }

    /* --- Network setup --- */
    net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, WIFI_MGMT_EVENTS);
    net_mgmt_add_event_callback(&wifi_mgmt_cb);
    net_mgmt_init_event_callback(&net_mgmt_cb, net_mgmt_event_handler, NET_EVENT_IPV4_DHCP_BOUND);
    net_mgmt_add_event_callback(&net_mgmt_cb);

    k_sleep(K_SECONDS(1));
    wifi_connect();

    while (!context.connect_result)
    {
        k_sleep(K_MSEC(100));
    }

    if (!context.connected)
    {
        LOG_ERR("Failed to connect WiFi");
        return -1;
    }

    LOG_INF("WiFi connected successfully!");
    print_mac_address();

    LOG_INF("Waiting for DHCP...");

    while (!context.dhcp_bound)
    {
        k_sleep(K_MSEC(100));
    }

    LOG_INF("DHCP complete, testing network connectivity...");

    /* Test basic TCP connectivity to broker before trying MQTT */
    int test_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (test_sock < 0)
    {
        LOG_ERR("Failed to create test socket: %d", errno);
    }
    else
    {
        struct sockaddr_in test_addr;
        test_addr.sin_family = AF_INET;
        test_addr.sin_port = htons(MQTT_BROKER_PORT);
        inet_pton(AF_INET, MQTT_BROKER_ADDR, &test_addr.sin_addr);

        LOG_INF("Testing TCP connection to %s:%d...", MQTT_BROKER_ADDR, MQTT_BROKER_PORT);
        int conn_ret = connect(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr));
        if (conn_ret < 0)
        {
            LOG_ERR("TCP connection test failed: %d (errno: %d)", conn_ret, errno);
            LOG_ERR("This suggests network routing or firewall issue");
        }
        else
        {
            LOG_INF("TCP connection test SUCCEEDED!");
        }
        close(test_sock);
        k_sleep(K_MSEC(500));
    }

    LOG_INF("Starting MQTT connection...");

    if (mqtt_connect_broker() != 0)
    {
        LOG_ERR("Failed to connect to MQTT broker");
        return -1;
    }

    /* Wait a bit after connection before publishing */
    k_sleep(K_SECONDS(1));

    /* Publish initial message */
    const char *payload = "Hello from misogate!";
    struct mqtt_publish_param param;
    param.message.topic.topic.utf8 = (uint8_t *)MQTT_PUB_TOPIC;
    param.message.topic.topic.size = strlen(MQTT_PUB_TOPIC);
    param.message.payload.data = (uint8_t *)payload;
    param.message.payload.len = strlen(payload);
    param.message_id = 1234;
    param.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
    param.dup_flag = 0U;
    param.retain_flag = 0U;

    int ret = mqtt_publish(&client, &param);
    if (ret == 0)
    {
        LOG_INF("Published message to %s", MQTT_PUB_TOPIC);
    }
    else
    {
        LOG_ERR("Failed to publish: %d", ret);
    }

    /* Main MQTT loop with proper polling */
    struct pollfd fds[1];
    fds[0].fd = client.transport.tcp.sock;
    fds[0].events = POLLIN;

    while (1)
    {
        /* Poll for incoming data with 1 second timeout */
        ret = poll(fds, 1, 1000);
        if (ret > 0)
        {
            /* Data available, process it */
            ret = mqtt_input(&client);
            if (ret != 0)
            {
                LOG_ERR("mqtt_input error: %d", ret);
                break;
            }
        }
        else if (ret < 0)
        {
            LOG_ERR("poll error: %d", errno);
            break;
        }

        /* Send keep-alive ping if needed */
        ret = mqtt_live(&client);
        if (ret != 0 && ret != -EAGAIN)
        {
            LOG_ERR("mqtt_live error: %d", ret);
            break;
        }

        k_sleep(K_MSEC(100));
    }

    LOG_ERR("MQTT loop exited, disconnecting");
    mqtt_disconnect(&client, NULL);
    return 0;
}
