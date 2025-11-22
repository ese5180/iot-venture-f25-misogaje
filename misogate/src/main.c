#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include <stdlib.h>
#include <hw_id.h>
#include <modem/modem_info.h>

#include "json_payload.h"
#include "mqtt/mqtt.h"
#include <memfault/metrics/metrics.h>

/* Register log module */
LOG_MODULE_REGISTER(misogate, CONFIG_MISOGATE_LOG_LEVEL);

/* Macros used to subscribe to specific Zephyr NET management events. */
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define MODEM_FIRMWARE_VERSION_SIZE_MAX 50

#define FATAL_ERROR()                              \
    LOG_ERR("Fatal error! Rebooting the device."); \
    LOG_PANIC();                                   \
    IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

/* Zephyr NET management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

static char hw_id[HW_ID_LEN];

/* Forward declarations. */
static void shadow_update_work_fn(struct k_work *work);
static void connect_work_fn(struct k_work *work);
static void aws_iot_event_handler(const struct aws_iot_evt *const evt);

static K_WORK_DELAYABLE_DEFINE(shadow_update_work, shadow_update_work_fn);
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

static int aws_iot_client_init(void)
{
    int err;

    err = aws_iot_init(aws_iot_event_handler);
    if (err)
    {
        LOG_ERR("AWS IoT library could not be initialized, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    /* Initialize MQTT application topics */
    err = mqtt_init();
    if (err)
    {
        LOG_ERR("MQTT initialization failed, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    return 0;
}

static void shadow_update_work_fn(struct k_work *work)
{
    int err;
    char message[CONFIG_AWS_IOT_JSON_MESSAGE_SIZE_MAX] = {0};
    struct payload payload = {
        .state.reported.uptime = k_uptime_get(),
        .state.reported.app_version = CONFIG_MISOGATE_APP_VERSION,
    };
    struct aws_iot_data tx_data = {
        .qos = MQTT_QOS_0_AT_MOST_ONCE,
        .topic.type = AWS_IOT_SHADOW_TOPIC_UPDATE,
    };

    if (IS_ENABLED(CONFIG_MODEM_INFO))
    {
        char modem_version_temp[MODEM_FIRMWARE_VERSION_SIZE_MAX];

        err = modem_info_get_fw_version(modem_version_temp,
                                        ARRAY_SIZE(modem_version_temp));
        if (err)
        {
            LOG_ERR("modem_info_get_fw_version, error: %d", err);
            FATAL_ERROR();
            return;
        }

        payload.state.reported.modem_version = modem_version_temp;
    }

    err = json_payload_construct(message, sizeof(message), &payload);
    if (err)
    {
        LOG_ERR("json_payload_construct, error: %d", err);
        FATAL_ERROR();
        return;
    }

    tx_data.ptr = message;
    tx_data.len = strlen(message);

    LOG_INF("Publishing message: %s to AWS IoT shadow", message);

    err = aws_iot_send(&tx_data);
    if (err)
    {
        LOG_ERR("aws_iot_send, error: %d", err);
        FATAL_ERROR();
        return;
    }

    (void)k_work_reschedule(&shadow_update_work,
                            K_SECONDS(CONFIG_AWS_IOT_PUBLICATION_INTERVAL_SECONDS));
}

static void connect_work_fn(struct k_work *work)
{
    int err;
    const struct aws_iot_config config = {
        .client_id = hw_id,
    };

    LOG_INF("Connecting to AWS IoT");
    memfault_metrics_heartbeat_add(MEMFAULT_METRICS_KEY(wifi_connection_attempts), 1);

    err = aws_iot_connect(&config);
    if (err == -EAGAIN)
    {
        LOG_INF("Connection attempt timed out, "
                "Next connection retry in %d seconds",
                CONFIG_AWS_IOT_CONNECTION_RETRY_TIMEOUT_SECONDS);

        (void)k_work_reschedule(&connect_work,
                                K_SECONDS(CONFIG_AWS_IOT_CONNECTION_RETRY_TIMEOUT_SECONDS));
    }
    else if (err)
    {
        LOG_ERR("aws_iot_connect, error: %d", err);
        FATAL_ERROR();
    }
}

/* Functions that are executed on specific connection-related events. */
static void on_aws_iot_evt_connected(const struct aws_iot_evt *const evt)
{
    (void)k_work_cancel_delayable(&connect_work);

    if (evt->data.persistent_session)
    {
        LOG_WRN("Persistent session is enabled, using subscriptions "
                "from the previous session");
    }

    /* Mark image as working to avoid reverting to the former image after a reboot. */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    LOG_INF("Confirming image");
    boot_write_img_confirmed();
#endif

    /* Start sequential updates to AWS IoT. */
    (void)k_work_reschedule(&shadow_update_work, K_NO_WAIT);
}

static void on_aws_iot_evt_disconnected(void)
{
    (void)k_work_cancel_delayable(&shadow_update_work);
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_connected(void)
{
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_disconnected(void)
{
    (void)aws_iot_disconnect();
    (void)k_work_cancel_delayable(&connect_work);
    (void)k_work_cancel_delayable(&shadow_update_work);
}

/* Event handlers */

static void aws_iot_event_handler(const struct aws_iot_evt *const evt)
{
    switch (evt->type)
    {
    case AWS_IOT_EVT_CONNECTING:
        LOG_INF("AWS_IOT_EVT_CONNECTING");
        break;
    case AWS_IOT_EVT_CONNECTED:
        LOG_INF("AWS_IOT_EVT_CONNECTED");
        on_aws_iot_evt_connected(evt);
        break;
    case AWS_IOT_EVT_DISCONNECTED:
        LOG_INF("AWS_IOT_EVT_DISCONNECTED");
        on_aws_iot_evt_disconnected();
        break;
    case AWS_IOT_EVT_DATA_RECEIVED:
        LOG_INF("AWS_IOT_EVT_DATA_RECEIVED");
        /* Handle MQTT data received on application topics */
        mqtt_handle_received_data(evt);
        break;
    case AWS_IOT_EVT_PUBACK:
        LOG_INF("AWS_IOT_EVT_PUBACK, message ID: %d", evt->data.message_id);
        break;
    case AWS_IOT_EVT_PINGRESP:
        LOG_INF("AWS_IOT_EVT_PINGRESP");
        break;
    case AWS_IOT_EVT_ERROR:
        LOG_INF("AWS_IOT_EVT_ERROR, %d", evt->data.err);
        FATAL_ERROR();
        break;
    default:
        LOG_WRN("Unknown AWS IoT event type: %d", evt->type);
        break;
    }
}

static void l4_event_handler(struct net_mgmt_event_callback *cb,
                             uint32_t event,
                             struct net_if *iface)
{
    switch (event)
    {
    case NET_EVENT_L4_CONNECTED:
        LOG_INF("Network connectivity established");
        on_net_event_l4_connected();
        break;
    case NET_EVENT_L4_DISCONNECTED:
        LOG_INF("Network connectivity lost");
        on_net_event_l4_disconnected();
        break;
    default:
        /* Don't care */
        return;
    }
}

static void connectivity_event_handler(struct net_mgmt_event_callback *cb,
                                       uint32_t event,
                                       struct net_if *iface)
{
    if (event == NET_EVENT_CONN_IF_FATAL_ERROR)
    {
        LOG_ERR("NET_EVENT_CONN_IF_FATAL_ERROR");
        FATAL_ERROR();
        return;
    }
}

int main(void)
{
    LOG_INF("misogate started, firmware version: %s", CONFIG_MISOGATE_APP_VERSION);

    int err;

    /* Setup handler for Zephyr NET Connection Manager events. */
    net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
    net_mgmt_add_event_callback(&l4_cb);

    /* Setup handler for Zephyr NET Connection Manager Connectivity layer. */
    net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler, CONN_LAYER_EVENT_MASK);
    net_mgmt_add_event_callback(&conn_cb);

    LOG_INF("bringing network interface up and connecting to the network");

    err = conn_mgr_all_if_up(true);
    if (err)
    {
        LOG_ERR("conn_mgr_all_if_up, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    err = conn_mgr_all_if_connect(true);
    if (err)
    {
        LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    err = aws_iot_client_init();

    if (err)
    {
        LOG_ERR("aws_iot_client_init, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    return 0;
}
