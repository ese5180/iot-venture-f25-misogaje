#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"

/**
 * @brief Maximum number of sensor nodes supported
 */
#define MAX_NODES 3

/**
 * @brief Number of packets to learn baseline per node
 */
#define BASELINE_SAMPLES 20

/**
 * @brief Minimum anomaly threshold (m-uT) for position calculation
 */
#define POSITION_MIN_ANOM 2000.0f

/**
 * @brief 3D vector type for magnetic field components (in milli-microTesla)
 */
struct vec3_i32
{
    int32_t x;
    int32_t y;
    int32_t z;
};

/**
 * @brief 3D vector type for floating point calculations
 */
struct vec3_f
{
    float x;
    float y;
    float z;
};

/**
 * @brief Per-node state structure for baseline tracking and 3D field storage
 *
 * Now stores full 3D magnetic field vectors for proper dipole modeling.
 */
struct node_state
{
    /* Baseline tracking */
    bool have_baseline;
    uint32_t baseline_count;

    /* Baseline 3D field (average with no magnet present) */
    int64_t baseline_sum_x;
    int64_t baseline_sum_y;
    int64_t baseline_sum_z;
    struct vec3_i32 baseline_B; /* Learned baseline B vector in m-uT */
    int32_t baseline_absB;      /* Learned baseline |B| in m-uT (scalar) */

    /* Latest measurements */
    struct vec3_i32 last_B;     /* Last raw B vector in m-uT */
    struct vec3_i32 last_B_mag; /* Last magnet-only B (measured - baseline) */
    int32_t last_absB;          /* Last |B| in m-uT */
    int32_t last_dAbsB;         /* Last anomaly |B|-baseline in m-uT (for compatibility) */
    uint32_t last_seq;
};

/**
 * @brief Initialize the LoRa receiver module
 *
 * @return 0 on success, negative errno on failure
 */
int lora_receiver_init(void);

/**
 * @brief Start the LoRa receiver thread
 *
 * Call this after network/MQTT is ready so received data can be published
 */
void lora_receiver_start(void);

/**
 * @brief Position structure with x,y coordinates (0-1000 range)
 */
struct lora_position
{
    int x; /* 0-1000 */
    int y; /* 0-1000 */
    bool valid;
};

/**
 * @brief Calibration state
 */
typedef enum
{
    CALIB_STATE_IDLE,          /* Not yet started */
    CALIB_STATE_BASELINE,      /* Capturing baseline (no magnet present) */
    CALIB_STATE_WAITING_INPUT, /* Waiting for calibration points or START */
    CALIB_STATE_RUNNING,       /* Normal operation */
} calib_state_t;

/**
 * @brief Start calibration process via serial console
 *
 * Call this after MQTT is connected. Two-phase calibration:
 * 1. Baseline capture: Records ambient field with NO magnet present
 * 2. Optional position calibration: Record known positions for lookup table
 */
void lora_start_calibration(void);

/**
 * @brief Check if calibration is complete and system is running
 *
 * @return true if system is in running state
 */
bool lora_is_running(void);

/**
 * @brief Get the estimated 2D position
 *
 * @param[out] pos Pointer to position struct to fill
 * @return 0 on success (valid position), -1 if not available
 */
int lora_get_position(struct lora_position *pos);

/**
 * @brief Get the estimated relative position (0-1000)
 *
 * @return Position 0-1000 (0=node1, 1000=node2), or -1 if not available
 * @deprecated Use lora_get_position() instead
 */
int lora_get_position_rel(void);

/**
 * @brief Get total valid packets received
 */
uint32_t lora_get_rx_count(void);

#endif /* LORA_H */
