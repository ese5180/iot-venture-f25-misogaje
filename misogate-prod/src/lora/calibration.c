/**
 * @file calibration.c
 * @brief Two-phase calibration system for magnetic position tracking
 *
 * Phase 1: Baseline Calibration (NO MAGNET present)
 *   - Captures the ambient magnetic field (Earth + hard iron offsets)
 *   - Averages multiple readings per sensor for stability
 *   - User confirms when baseline capture is complete
 *
 * Phase 2: Position Calibration (OPTIONAL)
 *   - User places magnet at known positions
 *   - System records the magnet-induced field at each position
 *   - Used for lookup-table-based position refinement
 *
 * After calibration completes, the system enters RUNNING state where:
 *   - B_magnet = B_measured - B_baseline
 *   - Position is estimated using dipole model + optional lookup table
 */

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calibration.h"
#include "position.h"

LOG_MODULE_REGISTER(calibration, LOG_LEVEL_INF);

/* ------------ State variables ------------ */

static calib_state_t g_calib_state = CALIB_STATE_IDLE;

/* Baseline data for each sensor */
static struct baseline_data g_baselines[MAX_NODES + 1];

/* Position calibration points */
static struct calib_point g_calib_points[MAX_CALIB_POINTS];
static int g_calib_point_count = 0;
static int g_current_calib_idx = -1;

/* Mutex for thread-safe access */
static K_MUTEX_DEFINE(calib_mutex);

/* Flag to control MQTT publishing */
static bool g_mqtt_publish_enabled = false;

/* Console input thread */
#define CONSOLE_STACK_SIZE 4096
#define CONSOLE_PRIORITY 6

static K_THREAD_STACK_DEFINE(console_stack, CONSOLE_STACK_SIZE);
static struct k_thread console_thread_data;
static k_tid_t console_thread_id;

/* Semaphore to signal thread to start */
static K_SEM_DEFINE(console_start_sem, 0, 1);

/* ------------ Console Helpers ------------ */

static void print_baseline_status(void) {
  printk("\nBaseline Status:\n");
  for (int i = 1; i <= MAX_NODES; i++) {
    struct baseline_data *bd = &g_baselines[i];
    if (bd->valid) {
      printk("  Sensor %d: READY  B_ambient=(%d, %d, %d) m-uT\n", i,
             bd->B_ambient.x, bd->B_ambient.y, bd->B_ambient.z);
    } else if (bd->readings_collected > 0) {
      printk("  Sensor %d: %d/%d readings\n", i, bd->readings_collected,
             BASELINE_READINGS_REQUIRED);
    } else {
      printk("  Sensor %d: waiting for data...\n", i);
    }
  }
  printk("\n");
}

static void print_baseline_help(void) {
  printk("\n");
  printk("==============================================\n");
  printk("     PHASE 1: BASELINE CALIBRATION\n");
  printk("==============================================\n");
  printk("\n");
  printk("IMPORTANT: Remove the magnet from the tracking area!\n");
  printk("\n");
  printk("The system is capturing the ambient magnetic field.\n");
  printk("This includes Earth's field and any local distortions.\n");
  printk("\n");
  printk("Commands:\n");
  printk("  STATUS  - Show baseline capture progress\n");
  printk("  DONE    - Finish baseline calibration\n");
  printk("  RESTART - Clear and restart baseline capture\n");
  printk("\n");
  printk("Baseline automatically captures from incoming sensor data.\n");
  printk("Wait until all sensors show READY, then type DONE.\n");
  printk("==============================================\n");
  printk("\n");
  print_baseline_status();
  printk("> ");
}

static void print_position_calibration_help(void) {
  printk("\n");
  printk("==============================================\n");
  printk("     PHASE 2: POSITION CALIBRATION (OPTIONAL)\n");
  printk("==============================================\n");
  printk("\n");
  printk("Place the MAGNET at known positions and enter coordinates.\n");
  printk(
      "This improves accuracy but is optional if dipole model is accurate.\n");
  printk("\n");
  printk("Commands:\n");
  printk("  X Y     - Calibrate at position (X,Y) where X,Y are 0-1000\n");
  printk("            Example: '250 500' calibrates at (250, 500)\n");
  printk("  START   - Skip/finish calibration, begin tracking mode\n");
  printk("  STATUS  - Show current calibration points\n");
  printk("  CLEAR   - Clear all calibration points\n");
  printk("\n");
  printk("Position calibration is optional. Type START to skip.\n");
  printk("==============================================\n");
  printk("\n");
  printk("> ");
}

static void print_calibration_status(void) {
  printk("\nPosition Calibration Points: %d\n", g_calib_point_count);
  for (int i = 0; i < g_calib_point_count; i++) {
    struct calib_point *cp = &g_calib_points[i];
    printk("  Point %d: (%d, %d) -> ", i + 1, cp->x, cp->y);
    for (int nid = 1; nid <= MAX_NODES; nid++) {
      if (cp->node_valid[nid]) {
        printk("S%d:(%d,%d,%d) ", nid, cp->node_B_mag[nid].x,
               cp->node_B_mag[nid].y, cp->node_B_mag[nid].z);
      }
    }
    printk("\n");
  }
  printk("\n> ");
}

static bool check_all_baselines_ready(void) {
  int ready_count = 0;
  for (int i = 1; i <= MAX_NODES; i++) {
    if (g_baselines[i].valid) {
      ready_count++;
    }
  }
  /* Need at least 2 sensors with valid baselines */
  return ready_count >= 2;
}

/* ------------ Console Input Thread ------------ */

static void console_input_thread(void *p1, void *p2, void *p3) {
  ARG_UNUSED(p1);
  ARG_UNUSED(p2);
  ARG_UNUSED(p3);

  /* Wait for start signal */
  k_sem_take(&console_start_sem, K_FOREVER);

  /* Small delay to let other init messages settle */
  k_sleep(K_MSEC(500));

  /* Initialize console for line input */
  console_getline_init();

  printk("\n\n*** Console input ready ***\n");
  print_baseline_help();

  while (1) {
    char *line = console_getline();
    if (!line || strlen(line) == 0) {
      printk("> ");
      continue;
    }

    printk("Received: '%s'\n", line);

    /* Skip leading whitespace */
    char *cmd = line;
    while (*cmd && isspace((unsigned char)*cmd))
      cmd++;

    /* Convert to uppercase for comparison */
    char cmd_upper[64];
    int i;
    for (i = 0; cmd[i] && i < 63; i++) {
      cmd_upper[i] = toupper((unsigned char)cmd[i]);
    }
    cmd_upper[i] = '\0';

    k_mutex_lock(&calib_mutex, K_FOREVER);
    calib_state_t current_state = g_calib_state;
    k_mutex_unlock(&calib_mutex);

    /* ------------ BASELINE PHASE COMMANDS ------------ */
    if (current_state == CALIB_STATE_BASELINE) {
      if (strncmp(cmd_upper, "STATUS", 6) == 0) {
        k_mutex_lock(&calib_mutex, K_FOREVER);
        print_baseline_status();
        k_mutex_unlock(&calib_mutex);
        printk("> ");
      } else if (strncmp(cmd_upper, "DONE", 4) == 0) {
        k_mutex_lock(&calib_mutex, K_FOREVER);
        if (!check_all_baselines_ready()) {
          printk("Error: Need at least 2 sensors with valid baselines!\n");
          print_baseline_status();
        } else {
          printk("\n*** Baseline calibration complete! ***\n");
          g_calib_state = CALIB_STATE_WAITING_INPUT;
          print_position_calibration_help();
        }
        k_mutex_unlock(&calib_mutex);
      } else if (strncmp(cmd_upper, "RESTART", 7) == 0) {
        k_mutex_lock(&calib_mutex, K_FOREVER);
        memset(g_baselines, 0, sizeof(g_baselines));
        printk("Baseline data cleared. Restarting capture...\n");
        k_mutex_unlock(&calib_mutex);
        printk("> ");
      } else {
        printk("Unknown command. Type STATUS, DONE, or RESTART.\n");
        printk("> ");
      }
    }
    /* ------------ POSITION CALIBRATION PHASE COMMANDS ------------ */
    else if (current_state == CALIB_STATE_WAITING_INPUT) {
      if (strncmp(cmd_upper, "START", 5) == 0) {
        k_mutex_lock(&calib_mutex, K_FOREVER);

        printk("\n");
        printk("==============================================\n");
        printk("  STARTING TRACKING MODE\n");
        if (g_calib_point_count > 0) {
          printk("  %d position calibration points loaded\n",
                 g_calib_point_count);
        } else {
          printk("  Using dipole model only (no calibration points)\n");
        }
        printk("  MQTT publishing enabled\n");
        printk("==============================================\n");
        printk("\n");

        g_calib_state = CALIB_STATE_RUNNING;
        g_mqtt_publish_enabled = true;
        g_current_calib_idx = -1;

        k_mutex_unlock(&calib_mutex);

        /* Exit console thread */
        return;
      } else if (strncmp(cmd_upper, "STATUS", 6) == 0) {
        k_mutex_lock(&calib_mutex, K_FOREVER);
        print_calibration_status();
        k_mutex_unlock(&calib_mutex);
      } else if (strncmp(cmd_upper, "CLEAR", 5) == 0) {
        k_mutex_lock(&calib_mutex, K_FOREVER);
        g_calib_point_count = 0;
        g_current_calib_idx = -1;
        memset(g_calib_points, 0, sizeof(g_calib_points));
        printk("Calibration points cleared.\n");
        k_mutex_unlock(&calib_mutex);
        printk("> ");
      } else {
        /* Try to parse as "X Y" */
        int x, y;
        if (sscanf(cmd, "%d %d", &x, &y) == 2) {
          if (x < 0 || x > 1000 || y < 0 || y > 1000) {
            printk("Error: X and Y must be 0-1000\n");
            printk("> ");
          } else if (g_calib_point_count >= MAX_CALIB_POINTS) {
            printk("Error: Maximum calibration points reached (%d)\n",
                   MAX_CALIB_POINTS);
            printk("> ");
          } else {
            k_mutex_lock(&calib_mutex, K_FOREVER);

            int idx = g_calib_point_count;
            memset(&g_calib_points[idx], 0, sizeof(struct calib_point));
            g_calib_points[idx].x = x;
            g_calib_points[idx].y = y;
            g_current_calib_idx = idx;
            g_calib_point_count++;

            printk("\nCalibrating point %d at (%d, %d)...\n", idx + 1, x, y);
            printk("Place MAGNET at this position now.\n");
            printk("Waiting for sensor readings...\n");
            printk("(Need %d readings per sensor)\n\n",
                   CALIB_READINGS_PER_POINT);

            k_mutex_unlock(&calib_mutex);

            /* Wait for readings to be collected */
            k_sleep(K_SECONDS(15));

            printk("Calibration point %d recorded.\n", idx + 1);
            print_calibration_status();
          }
        } else {
          printk("Unknown command: %s\n", cmd);
          printk("Enter 'X Y' coordinates or 'START'\n");
          printk("> ");
        }
      }
    }
  }
}

/* ------------ Public API ------------ */

void calibration_init(void) {
  memset(g_baselines, 0, sizeof(g_baselines));
  memset(g_calib_points, 0, sizeof(g_calib_points));
  g_calib_point_count = 0;
  g_calib_state = CALIB_STATE_IDLE;
  g_current_calib_idx = -1;
  g_mqtt_publish_enabled = false;

  /* Create console input thread */
  console_thread_id = k_thread_create(
      &console_thread_data, console_stack, K_THREAD_STACK_SIZEOF(console_stack),
      console_input_thread, NULL, NULL, NULL, CONSOLE_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(console_thread_id, "console_in");
}

void calibration_start_console(void) {
  printk("Starting calibration mode...\n");
  printk("PHASE 1: Baseline calibration (remove magnet from area)\n");

  k_mutex_lock(&calib_mutex, K_FOREVER);
  g_calib_state = CALIB_STATE_BASELINE;
  g_mqtt_publish_enabled = false;
  k_mutex_unlock(&calib_mutex);

  /* Start console input thread */
  k_sem_give(&console_start_sem);
}

calib_state_t calibration_get_state(void) {
  calib_state_t state;
  k_mutex_lock(&calib_mutex, K_FOREVER);
  state = g_calib_state;
  k_mutex_unlock(&calib_mutex);
  return state;
}

void calibration_set_state(calib_state_t state) {
  k_mutex_lock(&calib_mutex, K_FOREVER);
  g_calib_state = state;
  k_mutex_unlock(&calib_mutex);
}

bool calibration_is_running(void) {
  return calibration_get_state() == CALIB_STATE_RUNNING;
}

bool calibration_baseline_complete(void) {
  k_mutex_lock(&calib_mutex, K_FOREVER);
  bool complete = check_all_baselines_ready();
  k_mutex_unlock(&calib_mutex);
  return complete;
}

const struct baseline_data *calibration_get_baseline(int node_id) {
  if (node_id < 1 || node_id > MAX_NODES) {
    return NULL;
  }
  return &g_baselines[node_id];
}

const struct calib_point *calibration_get_points(int *count) {
  if (count) {
    *count = g_calib_point_count;
  }
  return g_calib_points;
}

void calibration_process_reading_3d(uint8_t node_id,
                                    const struct vec3_i32 *B_raw) {
  if (node_id < 1 || node_id > MAX_NODES || !B_raw) {
    return;
  }

  k_mutex_lock(&calib_mutex, K_FOREVER);

  /* ------------ BASELINE PHASE: Capture ambient field ------------ */
  if (g_calib_state == CALIB_STATE_BASELINE) {
    struct baseline_data *bd = &g_baselines[node_id];

    if (!bd->valid) {
      /* Accumulate readings for averaging */
      bd->sum_x += B_raw->x;
      bd->sum_y += B_raw->y;
      bd->sum_z += B_raw->z;
      bd->readings_collected++;

      printk("BASELINE: Sensor %u reading %d/%d B=(%d, %d, %d)\n", node_id,
             bd->readings_collected, BASELINE_READINGS_REQUIRED, B_raw->x,
             B_raw->y, B_raw->z);

      /* Check if we have enough readings */
      if (bd->readings_collected >= BASELINE_READINGS_REQUIRED) {
        bd->B_ambient.x = (int32_t)(bd->sum_x / bd->readings_collected);
        bd->B_ambient.y = (int32_t)(bd->sum_y / bd->readings_collected);
        bd->B_ambient.z = (int32_t)(bd->sum_z / bd->readings_collected);
        bd->valid = true;

        printk("BASELINE: Sensor %u COMPLETE B_ambient=(%d, %d, %d) m-uT\n",
               node_id, bd->B_ambient.x, bd->B_ambient.y, bd->B_ambient.z);

        /* Check if all baselines are ready */
        if (check_all_baselines_ready()) {
          printk("\n*** All baselines ready! Type DONE to continue. ***\n> ");
        }
      }
    }
  }
  /* ------------ POSITION CALIBRATION PHASE ------------ */
  else if (g_calib_state == CALIB_STATE_WAITING_INPUT &&
           g_current_calib_idx >= 0) {
    struct calib_point *cp = &g_calib_points[g_current_calib_idx];
    struct baseline_data *bd = &g_baselines[node_id];

    /* Only collect if this node hasn't finished and has valid baseline */
    if (!cp->node_valid[node_id] && bd->valid) {
      /* Compute magnet-induced field: B_mag = B_raw - B_baseline */
      int32_t B_mag_x = B_raw->x - bd->B_ambient.x;
      int32_t B_mag_y = B_raw->y - bd->B_ambient.y;
      int32_t B_mag_z = B_raw->z - bd->B_ambient.z;

      /* Accumulate for averaging */
      cp->sum_x[node_id] += B_mag_x;
      cp->sum_y[node_id] += B_mag_y;
      cp->sum_z[node_id] += B_mag_z;
      cp->reading_count[node_id]++;

      /* Also track scalar for legacy compatibility */
      int32_t absB = position_compute_absB(B_mag_x, B_mag_y, B_mag_z);
      cp->reading_sum[node_id] += absB;

      printk("CALIB: Sensor %u reading %d/%d B_mag=(%d, %d, %d)\n", node_id,
             cp->reading_count[node_id], CALIB_READINGS_PER_POINT, B_mag_x,
             B_mag_y, B_mag_z);

      /* Check if we have enough readings */
      if (cp->reading_count[node_id] >= CALIB_READINGS_PER_POINT) {
        int n = cp->reading_count[node_id];
        cp->node_B_mag[node_id].x = (int32_t)(cp->sum_x[node_id] / n);
        cp->node_B_mag[node_id].y = (int32_t)(cp->sum_y[node_id] / n);
        cp->node_B_mag[node_id].z = (int32_t)(cp->sum_z[node_id] / n);
        cp->node_absB[node_id] = (int32_t)(cp->reading_sum[node_id] / n);
        cp->node_valid[node_id] = true;

        printk("CALIB: Sensor %u DONE avg B_mag=(%d, %d, %d) m-uT\n", node_id,
               cp->node_B_mag[node_id].x, cp->node_B_mag[node_id].y,
               cp->node_B_mag[node_id].z);
      }
    }
  }

  k_mutex_unlock(&calib_mutex);
}

void calibration_process_reading(uint8_t node_id, int32_t absB) {
  /* Legacy scalar processing - minimal implementation for backwards compat */
  k_mutex_lock(&calib_mutex, K_FOREVER);

  if (g_calib_state == CALIB_STATE_WAITING_INPUT && g_current_calib_idx >= 0) {
    struct calib_point *cp = &g_calib_points[g_current_calib_idx];

    if (!cp->node_valid[node_id]) {
      cp->reading_sum[node_id] += absB;
      cp->reading_count[node_id]++;

      if (cp->reading_count[node_id] >= CALIB_READINGS_PER_POINT) {
        cp->node_absB[node_id] =
            (int32_t)(cp->reading_sum[node_id] / cp->reading_count[node_id]);
        cp->node_valid[node_id] = true;
      }
    }
  }

  k_mutex_unlock(&calib_mutex);
}

bool calibration_mqtt_publish_enabled(void) {
  bool enabled;
  k_mutex_lock(&calib_mutex, K_FOREVER);
  enabled = g_mqtt_publish_enabled;
  k_mutex_unlock(&calib_mutex);
  return enabled;
}

void calibration_lock(void) { k_mutex_lock(&calib_mutex, K_FOREVER); }

void calibration_unlock(void) { k_mutex_unlock(&calib_mutex); }
