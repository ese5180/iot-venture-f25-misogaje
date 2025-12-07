#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#include "lora.h"
#include "packet.h"
#include "../mqtt/mqtt.h"

LOG_MODULE_REGISTER(lora, CONFIG_MISOGATE_LOG_LEVEL);

/* LoRa thread stack and priority */
#define LORA_THREAD_STACK_SIZE 4096
#define LORA_THREAD_PRIORITY   7

/* Thread and stack definition */
static K_THREAD_STACK_DEFINE(lora_thread_stack, LORA_THREAD_STACK_SIZE);
static struct k_thread lora_thread_data;
static k_tid_t lora_thread_id;

/* Per-node state at the gateway */
static struct node_state g_nodes[MAX_NODES + 1];

/* RX statistics */
static uint32_t rx_ok = 0;

/* LoRa device handle */
static const struct device *lora_dev;

/* Flag to indicate if receiver should be running */
static bool receiver_running = false;

/* Compute |B| from components in m-uT using float math */
static int32_t compute_absB_m_uT(int32_t x, int32_t y, int32_t z)
{
    float fx = (float)x;
    float fy = (float)y;
    float fz = (float)z;
    float mag = sqrtf(fx * fx + fy * fy + fz * fz);
    if (mag > 2147483647.0f) {
        mag = 2147483647.0f;
    }
    return (int32_t)mag;
}

/* Update per-node baseline and anomaly for this frame */
static void update_node_state(const struct sensor_frame *f, int32_t absB)
{
    uint8_t nid = f->node_id;
    if (nid == 0 || nid > MAX_NODES) {
        LOG_WRN("Ignoring frame from node_id=%u (out of range)", nid);
        return;
    }

    struct node_state *ns = &g_nodes[nid];

    /* Learn baseline first N samples per node */
    if (!ns->have_baseline) {
        ns->baseline_sum_absB += absB;
        ns->baseline_count++;

        if (ns->baseline_count >= BASELINE_SAMPLES) {
            ns->baseline_absB =
                (int32_t)(ns->baseline_sum_absB / (int32_t)ns->baseline_count);
            ns->have_baseline = true;
            LOG_INF("Node %u baseline learned: |B| = %d m-uT",
                    nid, ns->baseline_absB);
        }
    }

    ns->last_absB = absB;
    ns->last_seq  = f->tx_seq;

    if (ns->have_baseline) {
        ns->last_dAbsB = absB - ns->baseline_absB;
    } else {
        ns->last_dAbsB = 0;
    }
}

/*
 * Estimate relative TBM position using node 1 and 2.
 * Returns:
 *   -1 if no valid estimate yet
 *    0..100 otherwise (0=node1, 100=node2).
 */
int lora_get_position_rel(void)
{
    struct node_state *n1 = &g_nodes[1];
    struct node_state *n2 = &g_nodes[2];

    if (!n1->have_baseline || !n2->have_baseline) {
        return -1; /* learning baselines */
    }

    float d0 = fabsf((float)n1->last_dAbsB);
    float d1 = fabsf((float)n2->last_dAbsB);
    float sum = d0 + d1;

    if (sum < POSITION_MIN_ANOM) {
        /* Magnet too far / weak to give meaningful position */
        return -1;
    }

    /* Barycentric between node1 and node2:
     * ratio = d1 / (d0 + d1)
     *  => 0 means all at node1, 1 means all at node2
     */
    float ratio = d1 / sum;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    int pos_rel = (int)(ratio * 100.0f + 0.5f); /* round to nearest int in [0,100] */
    if (pos_rel < 0)   pos_rel = 0;
    if (pos_rel > 100) pos_rel = 100;
    return pos_rel;
}

uint32_t lora_get_rx_count(void)
{
    return rx_ok;
}

/* Publish sensor data to MQTT */
static void publish_sensor_data(const struct sensor_frame *f, int32_t absB, 
                                int32_t dAbsB, int16_t rssi, int16_t snr, int pos_rel)
{
    char json_msg[512];
    int len;

    /* Format JSON with all sensor data */
    if (pos_rel >= 0) {
        len = snprintf(json_msg, sizeof(json_msg),
            "{"
            "\"node_id\":%u,"
            "\"tx_seq\":%u,"
            "\"x_uT_milli\":%d,"
            "\"y_uT_milli\":%d,"
            "\"z_uT_milli\":%d,"
            "\"absB_milli\":%d,"
            "\"dAbsB_milli\":%d,"
            "\"temp_c\":%.1f,"
            "\"rssi\":%d,"
            "\"snr\":%d,"
            "\"position_rel\":%d,"
            "\"rx_count\":%u"
            "}",
            (unsigned)f->node_id,
            (unsigned)f->tx_seq,
            f->x_uT_milli,
            f->y_uT_milli,
            f->z_uT_milli,
            absB,
            dAbsB,
            (float)f->temp_c_times10 / 10.0f,
            rssi,
            snr,
            pos_rel,
            rx_ok);
    } else {
        len = snprintf(json_msg, sizeof(json_msg),
            "{"
            "\"node_id\":%u,"
            "\"tx_seq\":%u,"
            "\"x_uT_milli\":%d,"
            "\"y_uT_milli\":%d,"
            "\"z_uT_milli\":%d,"
            "\"absB_milli\":%d,"
            "\"dAbsB_milli\":%d,"
            "\"temp_c\":%.1f,"
            "\"rssi\":%d,"
            "\"snr\":%d,"
            "\"position_rel\":null,"
            "\"rx_count\":%u"
            "}",
            (unsigned)f->node_id,
            (unsigned)f->tx_seq,
            f->x_uT_milli,
            f->y_uT_milli,
            f->z_uT_milli,
            absB,
            dAbsB,
            (float)f->temp_c_times10 / 10.0f,
            rssi,
            snr,
            rx_ok);
    }

    if (len > 0 && len < (int)sizeof(json_msg)) {
        int err = mqtt_publish_json(json_msg, len, MQTT_QOS_0_AT_MOST_ONCE);
        if (err) {
            LOG_WRN("Failed to publish LoRa data to MQTT: %d", err);
        } else {
            LOG_DBG("Published LoRa data to MQTT");
        }
    }
}

/* LoRa receiver thread function */
static void lora_receiver_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("LoRa receiver thread started");

    while (receiver_running) {
        uint8_t buf[64];
        int16_t rssi = 0, snr = 0;
        int len = lora_recv(lora_dev, buf, sizeof(buf), K_SECONDS(10), &rssi, &snr);

        if (len > 0) {
            struct sensor_frame f;
            if (packet_parse_secure_frame_encmac(buf, len, &f) == 0) {
                rx_ok++;

                /* Compute |B| and update per-node state */
                int32_t absB = compute_absB_m_uT(
                    f.x_uT_milli,
                    f.y_uT_milli,
                    f.z_uT_milli
                );

                update_node_state(&f, absB);

                struct node_state *ns = (f.node_id <= MAX_NODES)
                                        ? &g_nodes[f.node_id]
                                        : NULL;

                int32_t dAbsB = (ns && ns->have_baseline) ? ns->last_dAbsB : 0;

                /* Pretty temperature */
                int16_t t_abs = abs(f.temp_c_times10 % 10);

                LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                        "X=%d Y=%d Z=%d |B|=%d d|B|=%d m-uT "
                        "T=%d.%d C RSSI=%d SNR=%d len=%d",
                        (unsigned)rx_ok,
                        (unsigned)f.node_id,
                        (unsigned)f.tx_seq,
                        f.x_uT_milli,
                        f.y_uT_milli,
                        f.z_uT_milli,
                        absB,
                        dAbsB,
                        f.temp_c_times10 / 10,
                        t_abs,
                        rssi,
                        snr,
                        len);

                /* Get relative position estimate */
                int pos_rel = lora_get_position_rel();
                if (pos_rel >= 0) {
                    struct node_state *n1 = &g_nodes[1];
                    struct node_state *n2 = &g_nodes[2];
                    float d0 = fabsf((float)n1->last_dAbsB);
                    float d1 = fabsf((float)n2->last_dAbsB);

                    LOG_INF("POS_REL node1-2: %d (0=node1,100=node2) "
                            "d0=%.0f d1=%.0f m-uT",
                            pos_rel,
                            (double)d0,
                            (double)d1);
                } else {
                    LOG_INF("POS_REL: N/A (baselines not ready or anomalies too small)");
                }

                /* Publish to MQTT */
                publish_sensor_data(&f, absB, dAbsB, rssi, snr, pos_rel);

            } else {
                LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
            }
        } else if (len == 0) {
            LOG_DBG("LoRa RX timeout, waiting...");
        } else {
            LOG_WRN("LoRa RX error: %d", len);
        }
    }

    LOG_INF("LoRa receiver thread stopped");
}

int lora_receiver_init(void)
{
    lora_dev = DEVICE_DT_GET(DT_ALIAS(lora0));
    if (!device_is_ready(lora_dev)) {
        LOG_ERR("LoRa device not ready");
        return -ENODEV;
    }

    struct lora_modem_config cfg = {
        .frequency      = 915000000UL,
        .bandwidth      = BW_125_KHZ,
        .datarate       = SF_7,
        .coding_rate    = CR_4_5,
        .preamble_len   = 8,
        .tx_power       = 10,
        .tx             = false,   /* RX mode */
        .iq_inverted    = false,
        .public_network = true,
    };

    int ret = lora_config(lora_dev, &cfg);
    if (ret < 0) {
        LOG_ERR("LoRa config failed: %d", ret);
        return ret;
    }

    /* Clear node state */
    memset(g_nodes, 0, sizeof(g_nodes));

    LOG_INF("LoRa receiver initialized (915MHz, SF7, BW125kHz, Encrypt-then-MAC)");
    return 0;
}

void lora_receiver_start(void)
{
    if (receiver_running) {
        LOG_WRN("LoRa receiver already running");
        return;
    }

    receiver_running = true;

    lora_thread_id = k_thread_create(&lora_thread_data, lora_thread_stack,
                                     K_THREAD_STACK_SIZEOF(lora_thread_stack),
                                     lora_receiver_thread,
                                     NULL, NULL, NULL,
                                     LORA_THREAD_PRIORITY, 0, K_NO_WAIT);

    k_thread_name_set(lora_thread_id, "lora_rx");
    LOG_INF("LoRa receiver thread started");
}

