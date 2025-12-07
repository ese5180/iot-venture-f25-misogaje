#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <dk_buttons_and_leds.h>
#include <stdio.h>
#include <stdlib.h>
#include <hw_id.h>
#include <modem/modem_info.h>

#include "json_payload.h"
#include "mqtt/mqtt.h"

//#include <memfault/ncs.h>
//#include <memfault/core/data_export.h>

//extern int memfault_ncs_http_upload(void);


/* Register log module */
LOG_MODULE_REGISTER(misogate, CONFIG_MISOGATE_LOG_LEVEL);

/* Macros used to subscribe to specific Zephyr NET management events. */
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define FATAL_ERROR()                              \
    LOG_ERR("Fatal error! Rebooting the device."); \
    LOG_PANIC();                                   \
    IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

/* Zephyr NET management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

/* Forward declarations. */
static void connect_work_fn(struct k_work *work);
static void position_update_work_fn(struct k_work *work);
static void mqtt_process_work_fn(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);
static K_WORK_DELAYABLE_DEFINE(position_update_work, position_update_work_fn);
static K_WORK_DELAYABLE_DEFINE(mqtt_process_work, mqtt_process_work_fn);


static int counter = 0;
static bool mqtt_initialized = false;



// void on_net_event_l4_connected(void)
// {
//     LOG_INF("Network connected. Will try MQTT + Memfault.");

//     // 延迟连接 MQTT
//     (void)k_work_reschedule(&connect_work, K_SECONDS(3));

//     // 通知 Memfault 网络就绪
//     memfault_platform_boot();  // ✅ 让它自动上传缓存数据
// }




static void button_handler(uint32_t button_state, uint32_t has_changed)
{
    if (has_changed & DK_BTN1_MSK)
    {
        if (button_state & DK_BTN1_MSK)
        {
            if (counter < 255)
            {
                counter++;
                LOG_INF("Counter incremented: %d", counter);
            }
        }
    }

    if (has_changed & DK_BTN2_MSK)
    {
        if (button_state & DK_BTN2_MSK)
        {
            if (counter > 0)
            {
                counter--;
                LOG_INF("Counter decremented: %d", counter);
            }
        }
    }
}

static void position_update_work_fn(struct k_work *work)
{
    char message[64];
    int err;

    snprintf(message, sizeof(message), "{\"position\": %d}", counter);

    err = mqtt_publish_json(message, strlen(message), MQTT_QOS_0_AT_MOST_ONCE);
    if (err)
    {
        LOG_WRN("Failed to publish position update (might be disconnected), error: %d", err);
    }

    (void)k_work_reschedule(&position_update_work, K_SECONDS(5));
}

static void mqtt_process_work_fn(struct k_work *work)
{
    mqtt_app_input();
    k_work_reschedule(&mqtt_process_work, K_MSEC(100)); // Poll every 100ms
}

static void connect_work_fn(struct k_work *work)
{
    int err;

    if (!mqtt_initialized)
    {
        err = mqtt_app_init();
        if (err)
        {
            LOG_ERR("mqtt_app_init, error: %d", err);
            FATAL_ERROR();
            return;
        }
        mqtt_initialized = true;
    }

    LOG_INF("Connecting to MQTT broker...");

    err = mqtt_app_connect();
    if (err)
    {
        LOG_ERR("mqtt_app_connect failed: %d. Retrying...", err);
        (void)k_work_reschedule(&connect_work, K_SECONDS(5));
        return;
    }

    // Start processing MQTT input
    k_work_reschedule(&mqtt_process_work, K_NO_WAIT);

    /* Mark image as working to avoid reverting to the former image after a reboot. */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
    LOG_INF("Confirming image");
    boot_write_img_confirmed();
#endif

    /* Start sequential updates. */
    (void)k_work_reschedule(&position_update_work, K_SECONDS(5));
}

static void on_net_event_l4_connected(void)
{
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_disconnected(void)
{
    mqtt_app_disconnect();
    (void)k_work_cancel_delayable(&connect_work);
    (void)k_work_cancel_delayable(&mqtt_process_work);
    (void)k_work_cancel_delayable(&position_update_work);
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

    err = dk_buttons_init(button_handler);
    if (err)
    {
        LOG_ERR("dk_buttons_init, error: %d", err);
        FATAL_ERROR();
        return err;
    }

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

    return 0;
}
