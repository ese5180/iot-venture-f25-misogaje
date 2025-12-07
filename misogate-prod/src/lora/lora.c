#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "packet.h"

LOG_MODULE_REGISTER(misogate, LOG_LEVEL_INF);

/* ------------ LoRa config ------------ */

#define LORA_NODE      DT_ALIAS(lora0)
#define LORA_FREQ_HZ   915000000UL

/* ------------ Multi-node + trilateration config ------------ */

#define MAX_NODES          3      /* we support node IDs 1..3 for now */
#define BASELINE_WARMUP    20     /* packets per node before baseline is "locked in" */

/* Fixed physical positions (in meters or arbitrary units) of each node.
 * Adjust these to match your real geometry.
 *
 * Example:
 *  Node 1 at (0,0)
 *  Node 2 at (3,0)
 *  Node 3 at (1.5,2.6)   --> makes a nice triangle
 */
struct NodePos {
    float x;
    float y;
};

static const struct NodePos node_pos[MAX_NODES + 1] = {
    {0.0f, 0.0f},  /* index 0 unused */
    {0.0f, 0.0f},  /* node 1 */
    {0.0f, 100.0f},  /* node 2 */
    {50.0f, 50.0f},  /* node 3 */
};

/* Per-node running baseline + anomaly state */
struct NodeState {
    bool     seen;
    bool     have_baseline;
    uint32_t sample_count;
    int64_t  sum_absB;       /* for initial average baseline */
    int32_t  baseline_absB;  /* learned |B| baseline (m-uT) */

    int32_t  last_absB;      /* last |B| */
    int32_t  last_dAbsB;     /* last |B| - baseline (absolute) */
};

static struct NodeState g_nodes[MAX_NODES + 1];

/* ------------ Helpers ------------ */

/* magnitude of B vector, using X/Y/Z in m-uT units */
static int32_t compute_absB(const struct sensor_frame *f)
{
    int64_t x = f->x_uT_milli;
    int64_t y = f->y_uT_milli;
    int64_t z = f->z_uT_milli;

    long double xx = (long double)x * (long double)x;
    long double yy = (long double)y * (long double)y;
    long double zz = (long double)z * (long double)z;

    long double mag = sqrtl(xx + yy + zz);
    return (int32_t)mag;
}

/* Convert anomaly magnitude (|B|-baseline) to a weight.
 * We use w = d^3 to roughly match 1/r^3 behavior of a dipole field.
 */
static float anomaly_weight(int32_t dAbs)
{
    float d = fabsf((float)dAbs);
    if (d < 1.0f) {
        return 0.0f;
    }
    return d * d * d; /* α = 3 */
}

/* Weighted-centroid "trilateration" over all nodes with baselines.
 * Returns true if we have enough info to estimate a 2D position.
 */
static bool estimate_position_2D(float *out_x, float *out_y)
{
    float sum_w = 0.0f;
    float wx = 0.0f;
    float wy = 0.0f;
    int   active = 0;

    for (int nid = 1; nid <= MAX_NODES; nid++) {
        struct NodeState *ns = &g_nodes[nid];
        if (!ns->have_baseline) {
            continue;
        }

        float w = anomaly_weight(ns->last_dAbsB);
        if (w <= 0.0f) {
            continue;
        }

        sum_w += w;
        wx += w * node_pos[nid].x;
        wy += w * node_pos[nid].y;
        active++;
    }

    /* Need at least 2 nodes contributing to say anything useful */
    if (active < 2 || sum_w <= 0.0f) {
        return false;
    }

    *out_x = wx / sum_w;
    *out_y = wy / sum_w;
    return true;
}

/* Process a successfully authenticated sensor frame from one node:
 * - update baseline
 * - compute anomaly
 * - log detailed info
 * - update state for trilateration
 */
static void process_frame(const struct sensor_frame *f,
                          int16_t rssi,
                          int16_t snr,
                          int     pkt_len,
                          uint32_t *rx_ok_ptr)
{
    if (f->node_id == 0 || f->node_id > MAX_NODES) {
        LOG_WRN("Got frame from unexpected node_id=%u (MAX_NODES=%d)",
                (unsigned)f->node_id, MAX_NODES);
        return;
    }

    struct NodeState *ns = &g_nodes[f->node_id];

    int32_t absB = compute_absB(f);
    ns->seen = true;
    ns->last_absB = absB;

    if (!ns->have_baseline) {
        ns->sum_absB += absB;
        ns->sample_count++;

        if (ns->sample_count >= BASELINE_WARMUP) {
            ns->baseline_absB = (int32_t)(ns->sum_absB / (int64_t)ns->sample_count);
            ns->have_baseline = true;
            LOG_INF("Node %u baseline learned: |B| ≈ %d m-uT",
                    (unsigned)f->node_id, ns->baseline_absB);
        }
        ns->last_dAbsB = 0;
    } else {
        int32_t dAbs = abs(absB - ns->baseline_absB);
        ns->last_dAbsB = dAbs;
    }

    (*rx_ok_ptr)++;

    int16_t t_abs = abs(f->temp_c_times10 % 10);

    if (ns->have_baseline) {
        LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                "X=%d m-uT Y=%d m-uT Z=%d m-uT "
                "|B|=%d m-uT d|B|=%d m-uT "
                "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                (unsigned)(*rx_ok_ptr),
                (unsigned)f->node_id,
                (unsigned)f->tx_seq,
                f->x_uT_milli,
                f->y_uT_milli,
                f->z_uT_milli,
                absB,
                ns->last_dAbsB,
                f->temp_c_times10 / 10,
                t_abs,
                rssi,
                snr,
                pkt_len);
    } else {
        LOG_INF("SECURE PKT (learning baseline) rx_ok=%u node=%u tx_seq=%u "
                "X=%d m-uT Y=%d m-uT Z=%d m-uT "
                "|B|=%d m-uT "
                "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                (unsigned)(*rx_ok_ptr),
                (unsigned)f->node_id,
                (unsigned)f->tx_seq,
                f->x_uT_milli,
                f->y_uT_milli,
                f->z_uT_milli,
                absB,
                f->temp_c_times10 / 10,
                t_abs,
                rssi,
                snr,
                pkt_len);
    }

    /* After each valid frame, try to estimate 2D TBM position
     * using *all* nodes that have a baseline.
     */
    float pos_x, pos_y;
    if (estimate_position_2D(&pos_x, &pos_y)) {
        LOG_INF("POS_2D x=%.2f y=%.2f (triangle trilateration)", pos_x, pos_y);
    } else {
        LOG_INF("POS_2D unavailable (not enough baselines or anomalies)");
    }
}

/* ------------ Main ------------ */

void main(void)
{
    const struct device *lora = DEVICE_DT_GET(LORA_NODE);
    if (!device_is_ready(lora)) {
        LOG_ERR("LoRa device not ready");
        return;
    }

    struct lora_modem_config cfg = {
        .frequency      = LORA_FREQ_HZ,
        .bandwidth      = BW_125_KHZ,
        .datarate       = SF_7,
        .coding_rate    = CR_4_5,
        .preamble_len   = 8,
        .tx_power       = 10,
        .tx             = false,  /* RX gateway */
        .iq_inverted    = false,
        .public_network = true,
    };

    if (lora_config(lora, &cfg) < 0) {
        LOG_ERR("lora_config failed");
        return;
    }

    LOG_INF("misogate: RX (Encrypt-then-MAC, multi-node, 2D trilateration)");

    uint32_t rx_ok = 0;

    while (1) {
        uint8_t buf[64];
        int16_t rssi = 0, snr = 0;

        int len = lora_recv(lora, buf, sizeof(buf),
                            K_SECONDS(10), &rssi, &snr);

        if (len > 0) {
            struct sensor_frame f;
            if (packet_parse_secure_frame_encmac(buf, (size_t)len, &f) == 0) {
                process_frame(&f, rssi, snr, len, &rx_ok);
            } else {
                LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
            }
        } else {
            LOG_INF("misogate: waiting...");
        }
    }
}
