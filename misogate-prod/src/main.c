#include <dk_buttons_and_leds.h>
#include <hw_id.h>
#include <modem/modem_info.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/net_if.h>
#include <zephyr/sys/reboot.h>

#include "json_payload.h"
#include "lora/lora.h"
#include "mqtt/mqtt.h"

/* Register log module */
LOG_MODULE_REGISTER(misogate, CONFIG_MISOGATE_LOG_LEVEL);

/* Macros used to subscribe to specific Zephyr NET management events. */
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define FATAL_ERROR()                                                          \
  LOG_ERR("Fatal error! Rebooting the device.");                               \
  LOG_PANIC();                                                                 \
  IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

/* Zephyr NET management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

/* Forward declarations. */
static void connect_work_fn(struct k_work *work);
static void mqtt_process_work_fn(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);
static K_WORK_DELAYABLE_DEFINE(mqtt_process_work, mqtt_process_work_fn);

static bool mqtt_initialized = false;
static bool lora_initialized = false;
static bool lora_started = false;

static void mqtt_process_work_fn(struct k_work *work) {
  mqtt_app_input();
  k_work_reschedule(&mqtt_process_work, K_MSEC(100)); // Poll every 100ms
}

static void connect_work_fn(struct k_work *work) {
  int err;

  if (!mqtt_initialized) {
    err = mqtt_app_init();
    if (err) {
      LOG_ERR("mqtt_app_init, error: %d", err);
      FATAL_ERROR();
      return;
    }
    mqtt_initialized = true;
  }

  LOG_INF("Connecting to MQTT broker...");

  err = mqtt_app_connect_with_retries();
  if (err) {
    LOG_ERR("mqtt_app_connect_with_retries failed: %d. Retrying...", err);
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
    return;
  }

  /* Wait for MQTT connection to be fully established (CONNACK received) */
  err = mqtt_wait_connected(10000); /* 10 second timeout */
  if (err) {
    LOG_ERR("mqtt_wait_connected failed: %d. Retrying...", err);
    mqtt_app_disconnect();
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
    return;
  }

  /* Now MQTT is fully connected - start processing input for
   * keepalive/subscriptions */
  k_work_reschedule(&mqtt_process_work, K_NO_WAIT);

  /* Mark image as working to avoid reverting to the former image after a
   * reboot. */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
  LOG_INF("Confirming image");
  boot_write_img_confirmed();
#endif

  /* Start LoRa receiver and calibration now that MQTT is FULLY connected */
  if (lora_initialized && !lora_started) {
    LOG_INF("Starting LoRa receiver thread (MQTT confirmed connected)");
    lora_receiver_start();
    lora_started = true;

    /* Start calibration process - user must enter calibration points
     * via serial terminal before MQTT data will be published */
    LOG_INF("Starting calibration mode - see serial console for instructions");
    lora_start_calibration();
  }
}

static void on_net_event_l4_connected(void) {
  (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_disconnected(void) {
  mqtt_app_disconnect();
  (void)k_work_cancel_delayable(&connect_work);
  (void)k_work_cancel_delayable(&mqtt_process_work);
}

static void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t event,
                             struct net_if *iface) {
  switch (event) {
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
                                       uint32_t event, struct net_if *iface) {
  if (event == NET_EVENT_CONN_IF_FATAL_ERROR) {
    LOG_ERR("NET_EVENT_CONN_IF_FATAL_ERROR");
    FATAL_ERROR();
    return;
  }
}

int main(void) {
  LOG_INF("misogate started, firmware version: %s",
          CONFIG_MISOGATE_APP_VERSION);

  int err;

  /* Initialize LoRa receiver early (before network) */
  err = lora_receiver_init();
  if (err) {
    LOG_ERR("lora_receiver_init failed: %d", err);
    /* Continue without LoRa - WiFi/MQTT can still work */
  } else {
    lora_initialized = true;
    LOG_INF("LoRa receiver initialized successfully");
  }

  /* Print MAC address */
  struct net_if *iface = net_if_get_default();
  if (iface) {
    struct net_linkaddr *link_addr = net_if_get_link_addr(iface);
    if (link_addr && link_addr->len == 6) {
      LOG_INF("MAC address: %02x:%02x:%02x:%02x:%02x:%02x", link_addr->addr[0],
              link_addr->addr[1], link_addr->addr[2], link_addr->addr[3],
              link_addr->addr[4], link_addr->addr[5]);
    }
  }

  /* Setup handler for Zephyr NET Connection Manager events. */
  net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
  net_mgmt_add_event_callback(&l4_cb);

  /* Setup handler for Zephyr NET Connection Manager Connectivity layer. */
  net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler,
                               CONN_LAYER_EVENT_MASK);
  net_mgmt_add_event_callback(&conn_cb);

  LOG_INF("bringing network interface up and connecting to the network");

  err = conn_mgr_all_if_up(true);
  if (err) {
    LOG_ERR("conn_mgr_all_if_up, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  err = conn_mgr_all_if_connect(true);
  if (err) {
    LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  return 0;
}
