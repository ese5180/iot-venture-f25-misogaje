#ifndef LORA_H
#define LORA_H

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"

/**
 * @brief Maximum number of sensor nodes supported
 */
#define MAX_NODES          4

/**
 * @brief Number of packets to learn baseline per node
 */
#define BASELINE_SAMPLES   20

/**
 * @brief Minimum anomaly threshold (m-uT) for position calculation
 */
#define POSITION_MIN_ANOM  2000.0f

/**
 * @brief Per-node state structure for baseline tracking
 */
struct node_state {
    bool     have_baseline;
    uint32_t baseline_count;
    int64_t  baseline_sum_absB;  /* sum of |B| during baseline learning */
    int32_t  baseline_absB;      /* learned baseline |B| in m-uT */

    int32_t  last_absB;          /* last |B| in m-uT */
    int32_t  last_dAbsB;         /* last anomaly |B|-baseline in m-uT */
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
struct lora_position {
    int x;  /* 0-1000 */
    int y;  /* 0-1000 */
    bool valid;
};

/**
 * @brief Calibration state
 */
typedef enum {
    CALIB_STATE_IDLE,           /* Not yet started */
    CALIB_STATE_WAITING_INPUT,  /* Waiting for "X Y" or "START" */
    CALIB_STATE_RUNNING,        /* Normal operation */
} calib_state_t;

/**
 * @brief Start calibration process via serial console
 * 
 * Call this after MQTT is connected. User will enter calibration points
 * via serial in format "X Y" (0-100), then type "START" to begin.
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

