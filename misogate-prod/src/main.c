#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/sys/reboot.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>

#include "packet.h"
#include "mqtt/mqtt.h"

LOG_MODULE_REGISTER(misogate, CONFIG_MISOGATE_LOG_LEVEL);

/* --- Multi-node + relative position config --- */
#define MAX_NODES 4               /* we use node IDs 1 and 2 for now */
#define BASELINE_SAMPLES 20       /* packets per node to learn baseline */
#define POSITION_MIN_ANOM 2000.0f /* ignore if both anomalies tiny */

/* Macros used to subscribe to specific Zephyr NET management events. */
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)
#define CONN_LAYER_EVENT_MASK (NET_EVENT_CONN_IF_FATAL_ERROR)

#define FATAL_ERROR()                              \
    LOG_ERR("Fatal error! Rebooting the device."); \
    log_panic();                                   \
    IF_ENABLED(CONFIG_REBOOT, (sys_reboot(0)))

/* Per-node state at the gateway */
struct node_state
{
    bool have_baseline;
    uint32_t baseline_count;
    int64_t baseline_sum_absB; /* sum of |B| during baseline learning */
    int32_t baseline_absB;     /* learned baseline |B| in m-uT */

    int32_t last_absB;  /* last |B| in m-uT */
    int32_t last_dAbsB; /* last anomaly |B|-baseline in m-uT */
    uint32_t last_seq;
};

static struct node_state g_nodes[MAX_NODES + 1];

/* Current position (0-255 scale) - updated by LoRa receiver */
static volatile int current_position = -1; /* -1 means no valid position yet */

/* Zephyr NET management event callback structures. */
static struct net_mgmt_event_callback l4_cb;
static struct net_mgmt_event_callback conn_cb;

/* Forward declarations. */
static void connect_work_fn(struct k_work *work);
static void position_update_work_fn(struct k_work *work);
static void mqtt_process_work_fn(struct k_work *work);

static K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_fn);
static K_WORK_DELAYABLE_DEFINE(position_update_work, position_update_work_fn);
static K_WORK_DELAYABLE_DEFINE(mqtt_process_work, mqtt_process_work_fn);

static bool mqtt_initialized = false;

/* Compute |B| from components in m-uT using float math */
static int32_t compute_absB_m_uT(int32_t x, int32_t y, int32_t z)
{
    float fx = (float)x;
    float fy = (float)y;
    float fz = (float)z;
    float mag = sqrtf(fx * fx + fy * fy + fz * fz);
    if (mag > 2147483647.0f)
    {
        mag = 2147483647.0f;
    }
    return (int32_t)mag;
}

/* Update per-node baseline and anomaly for this frame */
static void update_node_state(const struct sensor_frame *f, int32_t absB)
{
    uint8_t nid = f->node_id;
    if (nid == 0 || nid > MAX_NODES)
    {
        LOG_WRN("Ignoring frame from node_id=%u (out of range)", nid);
        return;
    }

    struct node_state *ns = &g_nodes[nid];

    /* Learn baseline first N samples per node */
    if (!ns->have_baseline)
    {
        ns->baseline_sum_absB += absB;
        ns->baseline_count++;

        if (ns->baseline_count >= BASELINE_SAMPLES)
        {
            ns->baseline_absB =
                (int32_t)(ns->baseline_sum_absB / (int32_t)ns->baseline_count);
            ns->have_baseline = true;
            LOG_INF("Node %u baseline learned: |B| ≈ %d m-uT",
                    nid, ns->baseline_absB);
        }
    }

    ns->last_absB = absB;
    ns->last_seq = f->tx_seq;

    if (ns->have_baseline)
    {
        ns->last_dAbsB = absB - ns->baseline_absB;
    }
    else
    {
        ns->last_dAbsB = 0;
    }
}

/*
 * Estimate relative TBM position using node 1 and 2.
 * Returns:
 *   -1 if no valid estimate yet
 *    0..255 otherwise (scaled from 0=node1, 255=node2).
 */
static int estimate_position_0_255(void)
{
    struct node_state *n1 = &g_nodes[1];
    struct node_state *n2 = &g_nodes[2];

    if (!n1->have_baseline || !n2->have_baseline)
    {
        return -1; /* learning baselines */
    }

    float d0 = fabsf((float)n1->last_dAbsB);
    float d1 = fabsf((float)n2->last_dAbsB);
    float sum = d0 + d1;

    if (sum < POSITION_MIN_ANOM)
    {
        /* Magnet too far / weak to give meaningful position */
        return -1;
    }

    /* Barycentric between node1 and node2:
     * ratio = d1 / (d0 + d1)
     *  => 0 means all at node1, 1 means all at node2
     */
    float ratio = d1 / sum;
    if (ratio < 0.0f)
        ratio = 0.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;

    /* Scale to 0-255 range */
    int pos = (int)(ratio * 255.0f + 0.5f);
    if (pos < 0)
        pos = 0;
    if (pos > 255)
        pos = 255;
    return pos;
}

/* MQTT position update work handler - publishes position every 1 second */
static void position_update_work_fn(struct k_work *work)
{
    char message[64];
    int err;
    int pos = current_position;

    if (pos >= 0)
    {
        snprintf(message, sizeof(message), "{\"position\": %d}", pos);

        err = mqtt_publish_json(message, strlen(message), MQTT_QOS_0_AT_MOST_ONCE);
        if (err)
        {
            LOG_WRN("Failed to publish position update, error: %d", err);
        }
        else
        {
            LOG_INF("Published: %s", message);
        }
    }
    else
    {
        LOG_DBG("No valid position to publish yet");
    }

    /* Reschedule for 1 second */
    (void)k_work_reschedule(&position_update_work, K_SECONDS(1));
}

/* MQTT processing work handler */
static void mqtt_process_work_fn(struct k_work *work)
{
    mqtt_app_input();
    k_work_reschedule(&mqtt_process_work, K_MSEC(100)); /* Poll every 100ms */
}

/* MQTT connect work handler */
static void connect_work_fn(struct k_work *work)
{
    int err;

    if (!mqtt_initialized)
    {
        err = mqtt_app_init();
        if (err)
        {
            LOG_ERR("mqtt_app_init, error: %d", err);
            FATAL_ERROR();
            return;
        }
        mqtt_initialized = true;
    }

    LOG_INF("Connecting to MQTT broker...");

    err = mqtt_app_connect();
    if (err)
    {
        LOG_ERR("mqtt_app_connect failed: %d. Retrying...", err);
        (void)k_work_reschedule(&connect_work, K_SECONDS(5));
        return;
    }

    /* Start processing MQTT input */
    k_work_reschedule(&mqtt_process_work, K_NO_WAIT);

    /* Start position updates every 1 second */
    (void)k_work_reschedule(&position_update_work, K_SECONDS(1));
}

static void on_net_event_l4_connected(void)
{
    LOG_INF("Network connectivity established, scheduling MQTT connect...");
    (void)k_work_reschedule(&connect_work, K_SECONDS(5));
}

static void on_net_event_l4_disconnected(void)
{
    LOG_INF("Network connectivity lost");
    mqtt_app_disconnect();
    (void)k_work_cancel_delayable(&connect_work);
    (void)k_work_cancel_delayable(&mqtt_process_work);
    (void)k_work_cancel_delayable(&position_update_work);
}

static void l4_event_handler(struct net_mgmt_event_callback *cb,
                             uint32_t event,
                             struct net_if *iface)
{
    switch (event)
    {
    case NET_EVENT_L4_CONNECTED:
        LOG_INF("Network connectivity established");
        on_net_event_l4_connected();
        break;
    case NET_EVENT_L4_DISCONNECTED:
        LOG_INF("Network connectivity lost");
        on_net_event_l4_disconnected();
        break;
    default:
        return;
    }
}

static void connectivity_event_handler(struct net_mgmt_event_callback *cb,
                                       uint32_t event,
                                       struct net_if *iface)
{
    if (event == NET_EVENT_CONN_IF_FATAL_ERROR)
    {
        LOG_ERR("NET_EVENT_CONN_IF_FATAL_ERROR");
        FATAL_ERROR();
        return;
    }
}

/* LoRa receiver thread - runs in background processing sensor data */
static void lora_receiver_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    const struct device *lora = DEVICE_DT_GET(DT_ALIAS(lora0));
    if (!device_is_ready(lora))
    {
        LOG_ERR("LoRa device not ready");
        return;
    }

    struct lora_modem_config cfg = {
        .frequency = 915000000UL,
        .bandwidth = BW_125_KHZ,
        .datarate = SF_7,
        .coding_rate = CR_4_5,
        .preamble_len = 8,
        .tx_power = 10,
        .tx = false, /* RX mode */
        .iq_inverted = false,
        .public_network = true,
    };
    if (lora_config(lora, &cfg) < 0)
    {
        LOG_ERR("lora_config failed");
        return;
    }

    LOG_INF("LoRa receiver started (Encrypt-then-MAC, multi-node, position 0-255)");

    memset(g_nodes, 0, sizeof(g_nodes));

    uint32_t rx_ok = 0;

    while (1)
    {
        uint8_t buf[64];
        int16_t rssi = 0, snr = 0;
        int len = lora_recv(lora, buf, sizeof(buf), K_SECONDS(10), &rssi, &snr);

        if (len > 0)
        {
            struct sensor_frame f;
            if (packet_parse_secure_frame_encmac(buf, len, &f) == 0)
            {
                rx_ok++;

                /* Compute |B| and update per-node state */
                int32_t absB = compute_absB_m_uT(
                    f.x_uT_milli,
                    f.y_uT_milli,
                    f.z_uT_milli);

                update_node_state(&f, absB);

                /* pretty temperature */
                int16_t t_abs = abs(f.temp_c_times10 % 10);

                struct node_state *ns = (f.node_id <= MAX_NODES)
                                            ? &g_nodes[f.node_id]
                                            : NULL;

                int32_t dAbs = (ns && ns->have_baseline) ? ns->last_dAbsB : 0;

                LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                        "X=%d m-uT Y=%d m-uT Z=%d m-uT |B|=%d m-uT d|B|=%d m-uT "
                        "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                        (unsigned)rx_ok,
                        (unsigned)f.node_id,
                        (unsigned)f.tx_seq,
                        f.x_uT_milli,
                        f.y_uT_milli,
                        f.z_uT_milli,
                        absB,
                        dAbs,
                        f.temp_c_times10 / 10,
                        t_abs,
                        rssi,
                        snr,
                        len);

                /* Compute position 0-255 using nodes 1 and 2 */
                int pos = estimate_position_0_255();
                if (pos >= 0)
                {
                    current_position = pos;

                    struct node_state *n1 = &g_nodes[1];
                    struct node_state *n2 = &g_nodes[2];

                    float d0 = fabsf((float)n1->last_dAbsB);
                    float d1 = fabsf((float)n2->last_dAbsB);

                    LOG_INF("POS node1-2: %d (0=node1,255=node2) "
                            "d0=%.0f m-uT d1=%.0f m-uT",
                            pos,
                            (double)d0,
                            (double)d1);
                }
                else
                {
                    LOG_INF("POS node1-2: N/A (baselines not ready or anomalies too small)");
                }
            }
            else
            {
                LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
            }
        }
        else
        {
            LOG_DBG("LoRa: waiting...");
        }
    }
}

/* Define LoRa receiver thread */
K_THREAD_DEFINE(lora_thread, 2048, lora_receiver_thread, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
    LOG_INF("misogate gateway started, firmware version: %s", CONFIG_MISOGATE_APP_VERSION);

    int err;

    /* Setup handler for Zephyr NET Connection Manager events. */
    net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
    net_mgmt_add_event_callback(&l4_cb);

    /* Setup handler for Zephyr NET Connection Manager Connectivity layer. */
    net_mgmt_init_event_callback(&conn_cb, connectivity_event_handler, CONN_LAYER_EVENT_MASK);
    net_mgmt_add_event_callback(&conn_cb);

    LOG_INF("Bringing network interface up and connecting to WiFi...");

    err = conn_mgr_all_if_up(true);
    if (err)
    {
        LOG_ERR("conn_mgr_all_if_up, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    err = conn_mgr_all_if_connect(true);
    if (err)
    {
        LOG_ERR("conn_mgr_all_if_connect, error: %d", err);
        FATAL_ERROR();
        return err;
    }

    /* Main thread can now return - LoRa receiver runs in its own thread,
     * and MQTT/network handling is done via work queue */
    return 0;
}
