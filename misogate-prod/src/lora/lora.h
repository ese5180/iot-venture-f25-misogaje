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
 * @brief Get the estimated relative position (0-100)
 *
 * @return Position 0-100 (0=node1, 100=node2), or -1 if not available
 */
int lora_get_position_rel(void);

/**
 * @brief Get total valid packets received
 */
uint32_t lora_get_rx_count(void);

#endif /* LORA_H */

