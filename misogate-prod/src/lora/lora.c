/**
 * @file lora.c
 * @brief LoRa receiver and magnetic position tracking integration
 *
 * This module receives LoRa packets from magnetometer sensor nodes and
 * processes them for position estimation. It integrates:
 * - Baseline calibration (ambient field capture)
 * - Optional position calibration (lookup table)
 * - Dipole-model-based position estimation
 * - MQTT publishing of position data
 */

#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mqtt/mqtt.h"
#include "calibration.h"
#include "lora.h"
#include "packet.h"
#include "position.h"

LOG_MODULE_REGISTER(lora, LOG_LEVEL_INF);

/* ------------ LoRa config ------------ */

#define LORA_NODE DT_ALIAS(lora0)
#define LORA_FREQ_HZ 915000000UL

/* ------------ State variables ------------ */

/* Per-node running state */
static struct node_state g_nodes[MAX_NODES + 1];

/* LoRa device handle */
static const struct device *lora_dev;

/* Receiver thread */
#define LORA_STACK_SIZE 4096
#define LORA_PRIORITY 5

static K_THREAD_STACK_DEFINE(lora_stack, LORA_STACK_SIZE);
static struct k_thread lora_thread_data;
static k_tid_t lora_thread_id;

/* Semaphore to signal thread to start receiving */
static K_SEM_DEFINE(lora_start_sem, 0, 1);

/* Statistics */
static uint32_t rx_ok_count = 0;
static int last_position_rel = -1;

/* 2D position storage */
static struct lora_position current_position = {.x = 0, .y = 0, .valid = false};
static K_MUTEX_DEFINE(position_mutex);

/* Position publish timer (100ms) */
#define POSITION_PUBLISH_INTERVAL_MS 100
static void position_publish_work_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(position_publish_work, position_publish_work_fn);

/* ------------ Internal Helpers ------------ */

/**
 * Update node state with new 3D measurement and compute magnet-induced field.
 */
static void update_node_state(struct node_state *ns, uint8_t node_id,
                              const struct sensor_frame *f) {
  /* Store raw 3D measurement */
  ns->last_B.x = f->x_uT_milli;
  ns->last_B.y = f->y_uT_milli;
  ns->last_B.z = f->z_uT_milli;

  /* Compute scalar magnitude for logging/legacy */
  ns->last_absB =
      position_compute_absB(f->x_uT_milli, f->y_uT_milli, f->z_uT_milli);
  ns->last_seq = f->tx_seq;

  /* Get baseline from calibration module */
  const struct baseline_data *baseline = calibration_get_baseline(node_id);

  if (baseline && baseline->valid) {
    /* We have a valid baseline - compute magnet-induced field */
    ns->have_baseline = true;

    /* Copy baseline to node state for position estimation */
    ns->baseline_B.x = baseline->B_ambient.x;
    ns->baseline_B.y = baseline->B_ambient.y;
    ns->baseline_B.z = baseline->B_ambient.z;

    /* B_mag = B_raw - B_baseline */
    ns->last_B_mag.x = f->x_uT_milli - baseline->B_ambient.x;
    ns->last_B_mag.y = f->y_uT_milli - baseline->B_ambient.y;
    ns->last_B_mag.z = f->z_uT_milli - baseline->B_ambient.z;

    /* Scalar baseline difference for legacy code */
    int32_t baseline_scalar = position_compute_absB(
        baseline->B_ambient.x, baseline->B_ambient.y, baseline->B_ambient.z);
    ns->baseline_absB = baseline_scalar;
    ns->last_dAbsB = abs(ns->last_absB - baseline_scalar);
  } else {
    /* No baseline yet - just use first reading approach for compatibility */
    if (!ns->have_baseline) {
      ns->have_baseline = true;
      ns->baseline_B = ns->last_B;
      ns->baseline_absB = ns->last_absB;
    }

    /* Compute magnet field as difference from stored baseline */
    ns->last_B_mag.x = f->x_uT_milli - ns->baseline_B.x;
    ns->last_B_mag.y = f->y_uT_milli - ns->baseline_B.y;
    ns->last_B_mag.z = f->z_uT_milli - ns->baseline_B.z;

    ns->last_dAbsB = abs(ns->last_absB - ns->baseline_absB);
  }
}

/* ------------ Frame Processing ------------ */

static void process_frame(const struct sensor_frame *f, int16_t rssi,
                          int8_t snr, int pkt_len) {
  if (f->node_id == 0 || f->node_id > MAX_NODES) {
    LOG_WRN("Got frame from unexpected node_id=%u (MAX_NODES=%d)",
            (unsigned)f->node_id, MAX_NODES);
    return;
  }

  struct node_state *ns = &g_nodes[f->node_id];

  /* Update node state with new measurement */
  update_node_state(ns, f->node_id, f);

  rx_ok_count++;

  /* Get current calibration state */
  calib_state_t current_state = calibration_get_state();

  /* ------------ Calibration Data Collection ------------ */

  /* During baseline phase, send 3D readings to calibration module */
  if (current_state == CALIB_STATE_BASELINE) {
    struct vec3_i32 B_raw = {
        .x = f->x_uT_milli, .y = f->y_uT_milli, .z = f->z_uT_milli};
    calibration_process_reading_3d(f->node_id, &B_raw);
  }
  /* During position calibration phase */
  else if (current_state == CALIB_STATE_WAITING_INPUT) {
    struct vec3_i32 B_raw = {
        .x = f->x_uT_milli, .y = f->y_uT_milli, .z = f->z_uT_milli};
    calibration_process_reading_3d(f->node_id, &B_raw);
  }

  /* ------------ Logging ------------ */

  /* Only log detailed packet info when in RUNNING state */
  if (current_state == CALIB_STATE_RUNNING) {
    int16_t t_abs = abs(f->temp_c_times10 % 10);
    LOG_INF("PKT rx=%u node=%u seq=%u "
            "B=(%d,%d,%d) B_mag=(%d,%d,%d) m-uT "
            "T=%d.%d C RSSI=%d SNR=%d",
            (unsigned)rx_ok_count, (unsigned)f->node_id, (unsigned)f->tx_seq,
            f->x_uT_milli, f->y_uT_milli, f->z_uT_milli, ns->last_B_mag.x,
            ns->last_B_mag.y, ns->last_B_mag.z, f->temp_c_times10 / 10, t_abs,
            rssi, snr);
  }

  /* ------------ Position Estimation ------------ */

  /* Only estimate position when in running state */
  if (current_state != CALIB_STATE_RUNNING) {
    return;
  }

  int calib_count;
  const struct calib_point *calib_points = calibration_get_points(&calib_count);

  float pos_x, pos_y;
  if (position_estimate_2D(g_nodes, calib_points, calib_count, &pos_x,
                           &pos_y)) {
    LOG_INF("POS_2D x=%.1f y=%.1f", (double)pos_x, (double)pos_y);

    /* Clamp to 0-1000 range */
    int clamped_x = (int)pos_x;
    int clamped_y = (int)pos_y;
    if (clamped_x < 0)
      clamped_x = 0;
    if (clamped_x > 1000)
      clamped_x = 1000;
    if (clamped_y < 0)
      clamped_y = 0;
    if (clamped_y > 1000)
      clamped_y = 1000;

    /* Update position with mutex protection */
    k_mutex_lock(&position_mutex, K_FOREVER);
    current_position.x = clamped_x;
    current_position.y = clamped_y;
    current_position.valid = true;
    k_mutex_unlock(&position_mutex);

    last_position_rel = clamped_x;
  } else {
    LOG_DBG("POS_2D unavailable (not enough data)");
  }
}

/* ------------ Position Publish Work ------------ */

static void position_publish_work_fn(struct k_work *work) {
  ARG_UNUSED(work);

  /* Only publish if enabled */
  if (!calibration_mqtt_publish_enabled()) {
    k_work_reschedule(&position_publish_work,
                      K_MSEC(POSITION_PUBLISH_INTERVAL_MS));
    return;
  }

  struct lora_position pos;

  k_mutex_lock(&position_mutex, K_FOREVER);
  pos = current_position;
  k_mutex_unlock(&position_mutex);

  if (pos.valid && mqtt_is_connected()) {
    char json_buf[64];
    int len = snprintf(json_buf, sizeof(json_buf), "{\"x\":%d,\"y\":%d}", pos.x,
                       pos.y);

    if (len > 0 && len < (int)sizeof(json_buf)) {
      int err = mqtt_publish_json(json_buf, len, MQTT_QOS_0_AT_MOST_ONCE);
      if (err) {
        LOG_WRN("Position publish failed: %d", err);
      } else {
        LOG_DBG("Published position: %s", json_buf);
      }
    }
  }

  k_work_reschedule(&position_publish_work,
                    K_MSEC(POSITION_PUBLISH_INTERVAL_MS));
}

/* ------------ LoRa Receiver Thread ------------ */

static void lora_receiver_thread(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  /* Wait for start signal */
  k_sem_take(&lora_start_sem, K_FOREVER);

  printk("LoRa receiver thread started\n");

  while (1) {
    uint8_t buf[64];
    int16_t rssi = 0;
    int8_t snr = 0;

    int len = lora_recv(lora_dev, buf, sizeof(buf), K_SECONDS(10), &rssi, &snr);

    if (len > 0) {
      struct sensor_frame f;
      if (packet_parse_secure_frame_encmac(buf, (size_t)len, &f) == 0) {
        process_frame(&f, rssi, snr, len);
      } else {
        /* Only log security drops when running (not during calibration) */
        if (calibration_is_running()) {
          LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
        }
      }
    } else if (len < 0 && len != -EAGAIN) {
      LOG_ERR("LoRa recv error: %d", len);
    }
  }
}

/* ------------ Public API ------------ */

int lora_receiver_init(void) {
  /* Initialize node state */
  memset(g_nodes, 0, sizeof(g_nodes));

  /* Initialize submodules */
  calibration_init();
  position_init();

  lora_dev = DEVICE_DT_GET(LORA_NODE);
  if (!device_is_ready(lora_dev)) {
    LOG_ERR("LoRa device not ready");
    return -ENODEV;
  }

  struct lora_modem_config cfg = {
      .frequency = LORA_FREQ_HZ,
      .bandwidth = BW_125_KHZ,
      .datarate = SF_7,
      .coding_rate = CR_4_5,
      .preamble_len = 8,
      .tx_power = 10,
      .tx = false, /* RX gateway */
      .iq_inverted = false,
      .public_network = true,
  };

  if (lora_config(lora_dev, &cfg) < 0) {
    LOG_ERR("lora_config failed");
    return -EIO;
  }

  LOG_INF("LoRa configured: %lu Hz, BW125, SF7, CR4/5",
          (unsigned long)LORA_FREQ_HZ);

  /* Create receiver thread (suspended until lora_receiver_start is called) */
  lora_thread_id = k_thread_create(
      &lora_thread_data, lora_stack, K_THREAD_STACK_SIZEOF(lora_stack),
      lora_receiver_thread, NULL, NULL, NULL, LORA_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(lora_thread_id, "lora_rx");

  return 0;
}

void lora_receiver_start(void) {
  printk("Starting LoRa receiver\n");
  k_sem_give(&lora_start_sem);

  /* Start position publishing at 100ms intervals */
  k_work_reschedule(&position_publish_work,
                    K_MSEC(POSITION_PUBLISH_INTERVAL_MS));
}

void lora_start_calibration(void) { calibration_start_console(); }

bool lora_is_running(void) { return calibration_is_running(); }

int lora_get_position(struct lora_position *pos) {
  if (!pos) {
    return -1;
  }

  k_mutex_lock(&position_mutex, K_FOREVER);
  *pos = current_position;
  k_mutex_unlock(&position_mutex);

  return pos->valid ? 0 : -1;
}

int lora_get_position_rel(void) { return last_position_rel; }

uint32_t lora_get_rx_count(void) { return rx_ok_count; }
