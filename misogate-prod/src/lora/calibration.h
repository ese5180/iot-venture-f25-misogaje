#ifndef CALIBRATION_H
#define CALIBRATION_H

#include "lora.h"
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

/* ------------ Calibration config ------------ */

#define MAX_CALIB_POINTS 20
#define BASELINE_READINGS_REQUIRED                                             \
  10 /* Number of readings to average for baseline */
#define CALIB_READINGS_PER_POINT                                               \
  5 /* Number of readings to average per calibration point */

/* ------------ Calibration data structures ------------ */

/**
 * @brief Baseline calibration data for a single sensor node
 *
 * Stores the ambient magnetic field vector measured with NO magnet present.
 * This represents Earth's field + any hard iron offsets at the sensor.
 */
struct baseline_data {
  bool valid;
  int readings_collected;
  int64_t sum_x;
  int64_t sum_y;
  int64_t sum_z;
  struct vec3_i32 B_ambient; /* Averaged ambient field in m-uT */
};

/**
 * @brief Calibration point for lookup-table position estimation
 *
 * Stores the user-specified position and measured 3D field vectors.
 * The stored field is the magnet-induced field (measured - baseline).
 */
struct calib_point {
  int x; /* User-specified X (0-1000) */
  int y; /* User-specified Y (0-1000) */

  /* 3D magnet-induced field at each sensor (after subtracting baseline) */
  struct vec3_i32 node_B_mag[MAX_NODES + 1];
  bool node_valid[MAX_NODES +
                  1]; /* Whether we have valid reading for each node */

  /* Accumulation for averaging */
  int reading_count[MAX_NODES + 1];
  int64_t sum_x[MAX_NODES + 1];
  int64_t sum_y[MAX_NODES + 1];
  int64_t sum_z[MAX_NODES + 1];

  /* Legacy: scalar |B| for backwards compatibility */
  int32_t node_absB[MAX_NODES + 1];
  int64_t reading_sum[MAX_NODES + 1];
};

/* ------------ Public API ------------ */

/**
 * @brief Initialize the calibration module
 */
void calibration_init(void);

/**
 * @brief Start the calibration console input thread
 */
void calibration_start_console(void);

/**
 * @brief Get current calibration state
 *
 * @return Current calibration state
 */
calib_state_t calibration_get_state(void);

/**
 * @brief Set calibration state
 *
 * @param state New state to set
 */
void calibration_set_state(calib_state_t state);

/**
 * @brief Check if system is in running state
 *
 * @return true if running, false otherwise
 */
bool calibration_is_running(void);

/**
 * @brief Check if baseline calibration is complete
 *
 * @return true if all sensors have valid baselines
 */
bool calibration_baseline_complete(void);

/**
 * @brief Get baseline data for a sensor
 *
 * @param node_id Node ID (1 to MAX_NODES)
 * @return Pointer to baseline data, or NULL if invalid ID
 */
const struct baseline_data *calibration_get_baseline(int node_id);

/**
 * @brief Get calibration points array and count
 *
 * @param count Output pointer for point count
 * @return Pointer to calibration points array
 */
const struct calib_point *calibration_get_points(int *count);

/**
 * @brief Process a sensor reading for calibration (3D version)
 *
 * Called from the frame processing logic when a new reading arrives.
 * During baseline phase: accumulates ambient field readings
 * During calibration phase: accumulates magnet-present readings
 *
 * @param node_id Node ID that sent the reading
 * @param B_raw Raw 3D magnetic field vector
 */
void calibration_process_reading_3d(uint8_t node_id,
                                    const struct vec3_i32 *B_raw);

/**
 * @brief Process a sensor reading for calibration (legacy scalar version)
 *
 * @param node_id Node ID that sent the reading
 * @param absB Absolute magnetic field magnitude
 */
void calibration_process_reading(uint8_t node_id, int32_t absB);

/**
 * @brief Check if MQTT publishing is enabled
 *
 * @return true if publishing is enabled
 */
bool calibration_mqtt_publish_enabled(void);

/**
 * @brief Lock the calibration mutex
 */
void calibration_lock(void);

/**
 * @brief Unlock the calibration mutex
 */
void calibration_unlock(void);

#endif /* CALIBRATION_H */
