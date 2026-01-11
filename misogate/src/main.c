#include <hw_id.h>
#include <math.h>
#include <modem/modem_info.h>
#include <net/aws_iot.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_MEMFAULT)
#include <memfault/components.h>
#include <memfault/metrics/metrics.h>
#endif

#include "json_payload.h"
#include "mqtt/mqtt.h"

/* Register log module */
LOG_MODULE_REGISTER(misogate, CONFIG_MISOGATE_LOG_LEVEL);

// MMC5983MA Magnetometer Driver (from magsens)
#define MMC5983MA_ADDR 0x30

/* Register definitions */
#define MMC5983MA_REG_XOUT0 0x00
#define MMC5983MA_REG_XOUT1 0x01
#define MMC5983MA_REG_YOUT0 0x02
#define MMC5983MA_REG_YOUT1 0x03
#define MMC5983MA_REG_ZOUT0 0x04
#define MMC5983MA_REG_ZOUT1 0x05
#define MMC5983MA_REG_XYZOUT2 0x06
#define MMC5983MA_REG_TOUT 0x07
#define MMC5983MA_REG_STATUS 0x08
#define MMC5983MA_REG_CTRL0 0x09
#define MMC5983MA_REG_CTRL1 0x0A
#define MMC5983MA_REG_CTRL2 0x0B
#define MMC5983MA_REG_PRODUCT_ID 0x2F

/* Control register bits */
#define MMC5983MA_CTRL0_TM 0x01       /* Trigger measurement */
#define MMC5983MA_CTRL0_SET 0x08      /* SET operation */
#define MMC5983MA_CTRL0_RESET 0x10    /* RESET operation */
#define MMC5983MA_CTRL1_BW_100HZ 0x00 /* Bandwidth 100Hz */
#define MMC5983MA_CTRL2_CMM_EN 0x10   /* Continuous measurement mode */

/* Conversion constants */
#define MMC5983MA_OFFSET 131072           /* 18-bit midpoint (2^17) */
#define MMC5983MA_LSB_TO_GAUSS 0.0000625f /* 1 LSB = 0.0625 mG = 0.0000625 G   \
                                           */

/* Magnetometer state */
static const struct device *i2c_dev;
static bool mag_sensor_available = false;

static int mmc5983ma_init(void) {
#if !DT_NODE_EXISTS(DT_NODELABEL(i2c1))
  LOG_WRN("I2C1 not configured in devicetree - magnetometer disabled");
  return -ENODEV;
#else
  uint8_t product_id;
  uint8_t reg = MMC5983MA_REG_PRODUCT_ID;
  int ret;

  /* Get I2C device */
  i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
  if (!device_is_ready(i2c_dev)) {
    LOG_WRN("I2C device not ready - using simulated magnetometer values");
    return -ENODEV;
  }

  ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, &product_id, 1);
  if (ret != 0) {
    LOG_WRN(
        "Failed to read MMC5983MA Product ID (err %d) - using simulated values",
        ret);
    return ret;
  }

  if (product_id != 0x30) {
    LOG_WRN("Invalid MMC5983MA Product ID: 0x%02X (expected 0x30) - using "
            "simulated values",
            product_id);
    return -EIO;
  }

  LOG_INF("MMC5983MA detected, Product ID: 0x%02X", product_id);

  /* Perform SET operation (calibration) */
  uint8_t cmd[2] = {MMC5983MA_REG_CTRL0, MMC5983MA_CTRL0_SET};
  ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
  if (ret != 0) {
    LOG_WRN("Failed to send SET command (err %d)", ret);
    return ret;
  }
  k_msleep(1);

  /* Configure bandwidth to 100Hz */
  cmd[0] = MMC5983MA_REG_CTRL1;
  cmd[1] = MMC5983MA_CTRL1_BW_100HZ;
  ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
  if (ret != 0) {
    LOG_WRN("Failed to configure bandwidth (err %d)", ret);
    return ret;
  }

  LOG_INF("MMC5983MA initialized successfully");
  return 0;
#endif
}

// Read magnetometer data from MMC5983MA
static int mmc5983ma_read_mag(float *x_gauss, float *y_gauss, float *z_gauss) {
  uint8_t data[7];
  uint8_t reg = MMC5983MA_REG_XOUT0;
  int ret;
  int32_t x_raw, y_raw, z_raw;

  if (!mag_sensor_available || i2c_dev == NULL) {
    return -ENODEV;
  }

  /* Trigger single measurement */
  uint8_t cmd[2] = {MMC5983MA_REG_CTRL0, MMC5983MA_CTRL0_TM};
  ret = i2c_write(i2c_dev, cmd, 2, MMC5983MA_ADDR);
  if (ret != 0) {
    LOG_ERR("Failed to trigger measurement (err %d)", ret);
    return ret;
  }

  /* Wait for measurement to complete (~8ms) */
  k_msleep(10);

  /* Read 7 bytes: X0, X1, Y0, Y1, Z0, Z1, XYZ2 */
  ret = i2c_write_read(i2c_dev, MMC5983MA_ADDR, &reg, 1, data, 7);
  if (ret != 0) {
    LOG_ERR("Failed to read magnetometer data (err %d)", ret);
    return ret;
  }

  // Combine bytes into 18-bit values
  x_raw = ((uint32_t)data[0] << 10) | ((uint32_t)data[1] << 2) |
          ((data[6] >> 6) & 0x03);
  y_raw = ((uint32_t)data[2] << 10) | ((uint32_t)data[3] << 2) |
          ((data[6] >> 4) & 0x03);
  z_raw = ((uint32_t)data[4] << 10) | ((uint32_t)data[5] << 2) |
          ((data[6] >> 2) & 0x03);

  // Convert to Gauss
  x_raw -= MMC5983MA_OFFSET;
  y_raw -= MMC5983MA_OFFSET;
  z_raw -= MMC5983MA_OFFSET;

  *x_gauss = (float)x_raw * MMC5983MA_LSB_TO_GAUSS;
  *y_gauss = (float)y_raw * MMC5983MA_LSB_TO_GAUSS;
  *z_gauss = (float)z_raw * MMC5983MA_LSB_TO_GAUSS;

  return 0;
}

/* ==================
 * app code
 * ================== */

#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define MODEM_FIRMWARE_VERSION_SIZE_MAX 50

#define FATAL_ERROR()                                                          \
  LOG_ERR("Fatal error! Rebooting the device.");                               \
  LOG_PANIC();                                                                 \
  IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

static char hw_id[HW_ID_LEN];

static float default_mag_x = 0.25f;
static float default_mag_y = 0.35f;
static float default_mag_z = 0.45f;

/* Memfault tracking variables */
#if defined(CONFIG_MEMFAULT)
static uint32_t mag_read_success = 0;
static uint32_t mag_read_errors = 0;
static bool wifi_is_connected = false;
static bool mqtt_is_connected = false;
#endif

/* Forward declarations. */
static void shadow_update_work_fn(struct k_work *work);
static void connect_work_fn(struct k_work *work);
static void aws_iot_event_handler(const struct aws_iot_evt *const evt);

static K_WORK_DELAYABLE_DEFINE(shadow_update_work, shadow_update_work_fn);
static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);

#if defined(CONFIG_MEMFAULT)
// Update Memfault metrics with magnetometer and device state
static void update_memfault_metrics(float mag_x, float mag_y, float mag_z) {
  // Convert float to scaled integers (0.0001 Gauss units = x10000)
  int32_t x_scaled = (int32_t)(mag_x * 10000);
  int32_t y_scaled = (int32_t)(mag_y * 10000);
  int32_t z_scaled = (int32_t)(mag_z * 10000);
  float magnitude = sqrtf(mag_x * mag_x + mag_y * mag_y + mag_z * mag_z);
  uint32_t mag_scaled = (uint32_t)(magnitude * 10000);

  // magetosensor values
  memfault_metrics_heartbeat_set_signed(
      MEMFAULT_METRICS_KEY(mag_x_gauss_x10000), x_scaled);
  memfault_metrics_heartbeat_set_signed(
      MEMFAULT_METRICS_KEY(mag_y_gauss_x10000), y_scaled);
  memfault_metrics_heartbeat_set_signed(
      MEMFAULT_METRICS_KEY(mag_z_gauss_x10000), z_scaled);
  memfault_metrics_heartbeat_set_unsigned(
      MEMFAULT_METRICS_KEY(mag_magnitude_x10000), mag_scaled);

  // device health
  memfault_metrics_heartbeat_set_unsigned(
      MEMFAULT_METRICS_KEY(mag_read_success_count), mag_read_success);
  memfault_metrics_heartbeat_set_unsigned(
      MEMFAULT_METRICS_KEY(mag_read_error_count), mag_read_errors);
  memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(wifi_connected),
                                          wifi_is_connected ? 1 : 0);
  memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(mqtt_connected),
                                          mqtt_is_connected ? 1 : 0);
  memfault_metrics_heartbeat_set_unsigned(MEMFAULT_METRICS_KEY(uptime_seconds),
                                          (uint32_t)(k_uptime_get() / 1000));

  LOG_INF(
      "Memfault metrics updated: mag=[%d, %d, %d] x10000 Gauss, magnitude=%u",
      x_scaled, y_scaled, z_scaled, mag_scaled);
}
#endif

// read magnetometer data, use defaults if sensor unavailable
static bool read_magnetometer_with_fallback(float *x_gauss, float *y_gauss,
                                            float *z_gauss) {
  int ret;

  if (mag_sensor_available) {
    ret = mmc5983ma_read_mag(x_gauss, y_gauss, z_gauss);
    if (ret == 0) {
#if defined(CONFIG_MEMFAULT)
      mag_read_success++;
#endif
      LOG_DBG("Magnetometer read: X=%.4f, Y=%.4f, Z=%.4f Gauss",
              (double)*x_gauss, (double)*y_gauss, (double)*z_gauss);
      return true;
    }
    LOG_WRN("Magnetometer read failed (err %d), using default values", ret);
#if defined(CONFIG_MEMFAULT)
    mag_read_errors++;
#endif
  }

  *x_gauss = default_mag_x;
  *y_gauss = default_mag_y;
  *z_gauss = default_mag_z;

  default_mag_x += 0.001f;
  default_mag_y -= 0.0005f;
  default_mag_z += 0.0008f;

  if (default_mag_x > 1.0f)
    default_mag_x = -0.9f;
  if (default_mag_y < -1.0f)
    default_mag_y = 0.9f;
  if (default_mag_z > 1.0f)
    default_mag_z = -0.9f;

  LOG_DBG("Using default magnetometer values: X=%.4f, Y=%.4f, Z=%.4f Gauss",
          (double)*x_gauss, (double)*y_gauss, (double)*z_gauss);

  return false;
}

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
  /* Read magnetometer */
  float mag_x, mag_y, mag_z;
  bool real_sensor = read_magnetometer_with_fallback(&mag_x, &mag_y, &mag_z);

  /* Calculate magnitude */
  float magnitude = sqrtf(mag_x * mag_x + mag_y * mag_y + mag_z * mag_z);

  LOG_INF("Magnetometer [%s]: X=%.4f, Y=%.4f, Z=%.4f, Mag=%.4f Gauss",
          real_sensor ? "SENSOR" : "DEFAULT", (double)mag_x, (double)mag_y,
          (double)mag_z, (double)magnitude);

  /* Update Memfault with magnetometer data */
  update_memfault_metrics(mag_x, mag_y, mag_z);
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
  mqtt_is_connected = true;
  LOG_INF("MQTT connected - Memfault tracking updated");
#endif

  /* Start sequential updates to AWS IoT. */
  (void)k_work_reschedule(&shadow_update_work, K_NO_WAIT);
}

static void on_aws_iot_evt_disconnected(void) {
#if defined(CONFIG_MEMFAULT)
  mqtt_is_connected = false;
  LOG_INF("MQTT disconnected - Memfault tracking updated");
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
  wifi_is_connected = true;
  LOG_INF("WiFi connected - Memfault tracking updated");
#endif
  (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_disconnected(void) {
#if defined(CONFIG_MEMFAULT)
  wifi_is_connected = false;
  LOG_INF("WiFi disconnected - Memfault tracking updated");
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
  int err;

  LOG_INF("==============================================================");
  LOG_INF("~~MagNav~~");
  LOG_INF("Firmware version: %s", CONFIG_MISOGATE_APP_VERSION);
  LOG_INF("==============================================================");

  /* Initialize magnetometer */
  LOG_INF("Initializing MMC5983MA magnetometer...");
  err = mmc5983ma_init();
  if (err == 0) {
    mag_sensor_available = true;
    LOG_INF("MMC5983MA magnetometer: READY");
  } else {
    mag_sensor_available = false;
    LOG_WRN("MMC5983MA magnetometer: NOT AVAILABLE (using default values)");
    LOG_WRN("  Default values: X=%.2f, Y=%.2f, Z=%.2f Gauss",
            (double)default_mag_x, (double)default_mag_y,
            (double)default_mag_z);
  }

  /* Setup handler for Zephyr NET Connection Manager events. */
  net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
  net_mgmt_add_event_callback(&l4_cb);

  /* Setup handler for Zephyr NET Connection Manager Connectivity layer. */
  net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler,
                               CONN_LAYER_EVENT_MASK);
  net_mgmt_add_event_callback(&conn_cb);

  LOG_INF("Bringing network interface up and connecting to the network...");

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