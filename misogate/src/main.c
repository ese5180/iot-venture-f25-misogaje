#include <hw_id.h>
#include <modem/modem_info.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_MEMFAULT)
#include "memfault/memfault_integration.h"
#include <memfault/components.h>
#endif

#include "json_payload.h"
#include "mqtt/mqtt.h"

/* Register log module */
LOG_MODULE_REGISTER(misogate, CONFIG_MISOGATE_LOG_LEVEL);

/* Macros used to subscribe to specific Zephyr NET management events. */
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define MODEM_FIRMWARE_VERSION_SIZE_MAX 50

#define FATAL_ERROR()                                                          \
  LOG_ERR("Fatal error! Rebooting the device.");                               \
  LOG_PANIC();                                                                 \
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

#if defined(CONFIG_MEMFAULT)
static void memfault_upload_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(memfault_upload_work, memfault_upload_work_fn);

#define MEMFAULT_UPLOAD_INTERVAL_SEC 3600

static void memfault_upload_work_fn(struct k_work *work) {
  /* Upload any pending data */
  memfault_upload_data();

  /* Optionally check for OTA updates */
  memfault_check_for_ota();

  /* Reschedule */
  k_work_reschedule(&memfault_upload_work,
                    K_SECONDS(MEMFAULT_UPLOAD_INTERVAL_SEC));
}
#endif

static int aws_iot_client_init(void) {
  int err;

  err = aws_iot_init(aws_iot_event_handler);
  if (err) {
    LOG_ERR("AWS IoT library could not be initialized, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  /* Initialize MQTT application topics */
  err = mqtt_init();
  if (err) {
    LOG_ERR("MQTT initialization failed, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  return 0;
}

static void shadow_update_work_fn(struct k_work *work) {
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

  err = json_payload_construct(message, sizeof(message), &payload);
  if (err) {
    LOG_ERR("json_payload_construct, error: %d", err);
    FATAL_ERROR();
    return;
  }

  tx_data.ptr = message;
  tx_data.len = strlen(message);

  LOG_INF("Publishing message: %s to AWS IoT shadow", message);

  err = aws_iot_send(&tx_data);
  if (err) {
    LOG_ERR("aws_iot_send, error: %d", err);
#if defined(CONFIG_MEMFAULT)
    /* Record trace event for publish failure */
    MEMFAULT_TRACE_EVENT(aws_publish_failed);
#endif
    FATAL_ERROR();
    return;
  }

#if defined(CONFIG_MEMFAULT)
  // memfault_update_mag_metrics(x_gauss, y_gauss, z_gauss, magnitude);

  /* example with dummy values */
  static float demo_x = 0.3f;
  static float demo_y = 0.4f;
  static float demo_z = 0.5f;
  float demo_mag = sqrtf(demo_x * demo_x + demo_y * demo_y + demo_z * demo_z);

  memfault_update_mag_metrics(demo_x, demo_y, demo_z, demo_mag);

  /* vary values */
  demo_x += 0.001f;
  demo_y -= 0.001f;
#endif

  (void)k_work_reschedule(
      &shadow_update_work,
      K_SECONDS(CONFIG_AWS_IOT_PUBLICATION_INTERVAL_SECONDS));
}

static void connect_work_fn(struct k_work *work) {
  int err;
  const struct aws_iot_config config = {
      .client_id = hw_id,
  };

  LOG_INF("Connecting to AWS IoT");

  err = aws_iot_connect(&config);
  if (err == -EAGAIN) {
    LOG_INF("Connection attempt timed out, "
            "Next connection retry in %d seconds",
            CONFIG_AWS_IOT_CONNECTION_RETRY_TIMEOUT_SECONDS);

    (void)k_work_reschedule(
        &connect_work,
        K_SECONDS(CONFIG_AWS_IOT_CONNECTION_RETRY_TIMEOUT_SECONDS));
  } else if (err) {
    LOG_ERR("aws_iot_connect, error: %d", err);
    FATAL_ERROR();
  }
}

/* Functions that are executed on specific connection-related events. */
static void on_aws_iot_evt_connected(const struct aws_iot_evt *const evt) {
  (void)k_work_cancel_delayable(&connect_work);

  if (evt->data.persistent_session) {
    LOG_WRN("Persistent session is enabled, using subscriptions "
            "from the previous session");
  }

  /* Mark image as working to avoid reverting to the former image after a
   * reboot. */
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
  LOG_INF("Confirming image");
  boot_write_img_confirmed();
#endif

#if defined(CONFIG_MEMFAULT)
  /* Update MQTT connection state for Memfault */
  memfault_set_mqtt_connected(true);

  /* Start Memfault upload work */
  k_work_reschedule(&memfault_upload_work, K_SECONDS(30));
#endif

  /* Start sequential updates to AWS IoT. */
  (void)k_work_reschedule(&shadow_update_work, K_NO_WAIT);
}

static void on_aws_iot_evt_disconnected(void) {
#if defined(CONFIG_MEMFAULT)
  memfault_set_mqtt_connected(false);
  k_work_cancel_delayable(&memfault_upload_work);
#endif

  (void)k_work_cancel_delayable(&shadow_update_work);
  (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_aws_iot_evt_fota_done(const struct aws_iot_evt *const evt) {
  int err;

  /* Tear down MQTT connection. */
  (void)aws_iot_disconnect();
  (void)k_work_cancel_delayable(&connect_work);

  /* If modem FOTA has been carried out, the modem needs to be reinitialized.
   * This is carried out by bringing the network interface down/up.
   */
  if (evt->data.image & DFU_TARGET_IMAGE_TYPE_ANY_MODEM) {
    LOG_INF("Modem FOTA done, reinitializing the modem");

    err = conn_mgr_all_if_down(true);
    if (err) {
      LOG_ERR("conn_mgr_all_if_down, error: %d", err);
      FATAL_ERROR();
      return;
    }

    err = conn_mgr_all_if_up(true);
    if (err) {
      LOG_ERR("conn_mgr_all_if_up, error: %d", err);
      FATAL_ERROR();
      return;
    }

    err = conn_mgr_all_if_connect(true);
    if (err) {
      LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
      FATAL_ERROR();
      return;
    }
  } else if (evt->data.image & DFU_TARGET_IMAGE_TYPE_ANY_APPLICATION) {
    LOG_INF("Application FOTA done, rebooting");
    IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)));
  } else {
    LOG_WRN("Unexpected FOTA image type");
  }
}

static void on_net_event_l4_connected(void) {
#if defined(CONFIG_MEMFAULT)
  memfault_set_wifi_connected(true);
#endif
  (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_disconnected(void) {
#if defined(CONFIG_MEMFAULT)
  memfault_set_wifi_connected(false);
#endif
  (void)aws_iot_disconnect();
  (void)k_work_cancel_delayable(&connect_work);
  (void)k_work_cancel_delayable(&shadow_update_work);
}

/* Event handlers */

static void aws_iot_event_handler(const struct aws_iot_evt *const evt) {
  switch (evt->type) {
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
  case AWS_IOT_EVT_FOTA_START:
    LOG_INF("AWS_IOT_EVT_FOTA_START");
    break;
  case AWS_IOT_EVT_FOTA_ERASE_PENDING:
    LOG_INF("AWS_IOT_EVT_FOTA_ERASE_PENDING");
    break;
  case AWS_IOT_EVT_FOTA_ERASE_DONE:
    LOG_INF("AWS_FOTA_EVT_ERASE_DONE");
    break;
  case AWS_IOT_EVT_FOTA_DONE:
    LOG_INF("AWS_IOT_EVT_FOTA_DONE");
    on_aws_iot_evt_fota_done(evt);
    break;
  case AWS_IOT_EVT_FOTA_DL_PROGRESS:
    LOG_INF("AWS_IOT_EVT_FOTA_DL_PROGRESS, (%d%%)", evt->data.fota_progress);
    break;
  case AWS_IOT_EVT_ERROR:
    LOG_INF("AWS_IOT_EVT_ERROR, %d", evt->data.err);
    FATAL_ERROR();
    break;
  case AWS_IOT_EVT_FOTA_ERROR:
    LOG_INF("AWS_IOT_EVT_FOTA_ERROR");
    break;
  default:
    LOG_WRN("Unknown AWS IoT event type: %d", evt->type);
    break;
  }
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
    /* Don't care */
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

#if defined(CONFIG_MEMFAULT)
  /* Initialize Memfault SDK early */
  err = memfault_integration_init();
  if (err) {
    LOG_ERR("Memfault initialization failed: %d", err);
    /* Continue anyway - Memfault is not critical */
  }
#endif

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

  err = aws_iot_client_init();

  if (err) {
    LOG_ERR("aws_iot_client_init, error: %d", err);
    FATAL_ERROR();
    return err;
  }

  return 0;
}
