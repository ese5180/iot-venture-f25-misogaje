#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/console/console.h>

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#include "lora.h"
#include "packet.h"
#include "../mqtt/mqtt.h"

LOG_MODULE_REGISTER(lora, LOG_LEVEL_INF);

/* ------------ LoRa config ------------ */

#define LORA_NODE DT_ALIAS(lora0)
#define LORA_FREQ_HZ 915000000UL

/* ------------ Calibration config ------------ */

#define MAX_CALIB_POINTS 20
#define CALIB_READINGS_PER_POINT 5 /* Number of readings to average per calibration point */

/* Calibration point: stores the user-specified position and the measured readings */
struct calib_point
{
    int x;                              /* User-specified X (0-100 input, stored as 0-1000) */
    int y;                              /* User-specified Y (0-100 input, stored as 0-1000) */
    int32_t node_absB[MAX_NODES + 1];   /* Measured |B| for each node at this point */
    bool node_valid[MAX_NODES + 1];     /* Whether we have valid reading for each node */
    int reading_count[MAX_NODES + 1];   /* Number of readings collected per node */
    int64_t reading_sum[MAX_NODES + 1]; /* Sum of readings for averaging */
};

/* ------------ Node positions (fixed physical locations) ------------ */

struct NodePos
{
    float x;
    float y;
};

/* Node positions scaled to 0-1000 coordinate system */
static const struct NodePos node_pos[MAX_NODES + 1] = {
    {0.0f, 0.0f},     /* index 0 unused */
    {0.0f, 0.0f},     /* node 1 - bottom-left */
    {0.0f, 1000.0f},  /* node 2 - top-left */
    {500.0f, 500.0f}, /* node 3 - center */
    {1000.0f, 0.0f},  /* node 4 - bottom-right */
};

/* ------------ State variables ------------ */

/* Per-node running state */
static struct node_state g_nodes[MAX_NODES + 1];

/* Calibration state */
static calib_state_t g_calib_state = CALIB_STATE_IDLE;
static struct calib_point g_calib_points[MAX_CALIB_POINTS];
static int g_calib_point_count = 0;
static int g_current_calib_idx = -1; /* Index of calibration point being collected */
static K_MUTEX_DEFINE(calib_mutex);

/* LoRa device handle */
static const struct device *lora_dev;

/* Receiver thread */
#define LORA_STACK_SIZE 4096
#define LORA_PRIORITY 5

static K_THREAD_STACK_DEFINE(lora_stack, LORA_STACK_SIZE);
static struct k_thread lora_thread_data;
static k_tid_t lora_thread_id;

/* Console input thread */
#define CONSOLE_STACK_SIZE 4096
#define CONSOLE_PRIORITY 6

static K_THREAD_STACK_DEFINE(console_stack, CONSOLE_STACK_SIZE);
static struct k_thread console_thread_data;
static k_tid_t console_thread_id;

/* Semaphore to signal thread to start receiving */
static K_SEM_DEFINE(lora_start_sem, 0, 1);
static K_SEM_DEFINE(console_start_sem, 0, 1);

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

/* Flag to control MQTT publishing */
static bool g_mqtt_publish_enabled = false;

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

/* ------------ Position Estimation using Calibration Data ------------ */

/*
 * Inverse-distance weighted interpolation using calibration points.
 * For each calibration point, compute a "similarity" based on how close
 * the current readings are to the calibrated readings.
 * Weight = 1 / (distance in reading space)^2
 */
static bool estimate_position_calibrated(float *out_x, float *out_y)
{
    if (g_calib_point_count < 2)
    {
        return false;
    }

    float sum_w = 0.0f;
    float wx = 0.0f;
    float wy = 0.0f;

    for (int i = 0; i < g_calib_point_count; i++)
    {
        struct calib_point *cp = &g_calib_points[i];

        /* Compute distance in reading space between current readings and calibration point */
        float dist_sq = 0.0f;
        int valid_nodes = 0;

        for (int nid = 1; nid <= MAX_NODES; nid++)
        {
            if (!cp->node_valid[nid] || !g_nodes[nid].have_baseline)
            {
                continue;
            }

            /* Use absolute reading, not anomaly */
            float diff = (float)(g_nodes[nid].last_absB - cp->node_absB[nid]);
            dist_sq += diff * diff;
            valid_nodes++;
        }

        if (valid_nodes < 2)
        {
            continue;
        }

        /* Normalize by number of valid nodes */
        dist_sq /= (float)valid_nodes;

        /* Avoid division by zero - if very close, give high weight */
        if (dist_sq < 1.0f)
        {
            dist_sq = 1.0f;
        }

        /* Weight inversely proportional to distance squared */
        float w = 1.0f / dist_sq;

        sum_w += w;
        wx += w * (float)cp->x;
        wy += w * (float)cp->y;
    }

    if (sum_w <= 0.0f)
    {
        return false;
    }

    *out_x = wx / sum_w;
    *out_y = wy / sum_w;
    return true;
}

/* Fallback: weighted-centroid based on anomaly magnitude (original algorithm) */
static float anomaly_weight(int32_t dAbs)
{
    float d = fabsf((float)dAbs);
    if (d < 1.0f)
    {
        return 0.0f;
    }
    return d * d * d; /* α = 3 for dipole falloff */
}

static bool estimate_position_anomaly(float *out_x, float *out_y)
{
    float sum_w = 0.0f;
    float wx = 0.0f;
    float wy = 0.0f;
    int active = 0;

    for (int nid = 1; nid <= MAX_NODES; nid++)
    {
        struct node_state *ns = &g_nodes[nid];
        if (!ns->have_baseline)
        {
            continue;
        }

        float w = anomaly_weight(ns->last_dAbsB);
        if (w <= 0.0f)
        {
            continue;
        }

        sum_w += w;
        wx += w * node_pos[nid].x;
        wy += w * node_pos[nid].y;
        active++;
    }

    if (active < 2 || sum_w <= 0.0f)
    {
        return false;
    }

    *out_x = wx / sum_w;
    *out_y = wy / sum_w;
    return true;
}

/* Main position estimation function */
static bool estimate_position_2D(float *out_x, float *out_y)
{
    /* If we have calibration data, use calibrated estimation */
    if (g_calib_point_count >= 2)
    {
        return estimate_position_calibrated(out_x, out_y);
    }

    /* Fallback to anomaly-based estimation */
    return estimate_position_anomaly(out_x, out_y);
}

/* ------------ Frame Processing ------------ */

static void process_frame(const struct sensor_frame *f,
                          int16_t rssi,
                          int8_t snr,
                          int pkt_len)
{
    if (f->node_id == 0 || f->node_id > MAX_NODES)
    {
        LOG_WRN("Got frame from unexpected node_id=%u (MAX_NODES=%d)",
                (unsigned)f->node_id, MAX_NODES);
        return;
    }

    struct node_state *ns = &g_nodes[f->node_id];

    int32_t absB = compute_absB(f);
    ns->last_absB = absB;
    ns->last_seq = f->tx_seq;

    /* Mark that we have at least one reading */
    if (!ns->have_baseline)
    {
        ns->have_baseline = true;
        ns->baseline_absB = absB; /* Use first reading as baseline */
    }

    /* Always update anomaly relative to baseline */
    ns->last_dAbsB = abs(absB - ns->baseline_absB);

    rx_ok_count++;

    /* Handle calibration data collection - check state first */
    k_mutex_lock(&calib_mutex, K_FOREVER);
    calib_state_t current_state = g_calib_state;

    if (current_state == CALIB_STATE_WAITING_INPUT && g_current_calib_idx >= 0)
    {
        struct calib_point *cp = &g_calib_points[g_current_calib_idx];

        /* Only collect if this node hasn't finished yet */
        if (!cp->node_valid[f->node_id])
        {
            /* Accumulate readings for averaging */
            cp->reading_sum[f->node_id] += absB;
            cp->reading_count[f->node_id]++;

            /* Use printk during calibration to not interfere with console */
            printk("CALIB: Node %u reading %d/%d |B|=%d\n",
                   f->node_id, cp->reading_count[f->node_id],
                   CALIB_READINGS_PER_POINT, absB);

            /* Check if we have enough readings for this node */
            if (cp->reading_count[f->node_id] >= CALIB_READINGS_PER_POINT)
            {
                cp->node_absB[f->node_id] = (int32_t)(cp->reading_sum[f->node_id] /
                                                      cp->reading_count[f->node_id]);
                cp->node_valid[f->node_id] = true;
                printk("CALIB: Node %u DONE avg |B|=%d m-uT\n",
                       f->node_id, cp->node_absB[f->node_id]);
            }
        }
        /* Node already done - silently ignore */
    }
    k_mutex_unlock(&calib_mutex);

    /* Only log packet details when in RUNNING state (after calibration) */
    if (current_state == CALIB_STATE_RUNNING)
    {
        int16_t t_abs = abs(f->temp_c_times10 % 10);
        LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                "X=%d m-uT Y=%d m-uT Z=%d m-uT "
                "|B|=%d m-uT d|B|=%d m-uT "
                "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                (unsigned)rx_ok_count,
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
    }

    /* Only estimate position when in running state */
    if (current_state != CALIB_STATE_RUNNING)
    {
        return;
    }

    float pos_x, pos_y;
    if (estimate_position_2D(&pos_x, &pos_y))
    {
        LOG_INF("POS_2D x=%.2f y=%.2f", (double)pos_x, (double)pos_y);

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
    }
    else
    {
        LOG_INF("POS_2D unavailable (not enough data)");
    }
}

/* ------------ Position Publish Work ------------ */

static void position_publish_work_fn(struct k_work *work)
{
    ARG_UNUSED(work);

    /* Only publish if enabled */
    if (!g_mqtt_publish_enabled)
    {
        k_work_reschedule(&position_publish_work, K_MSEC(POSITION_PUBLISH_INTERVAL_MS));
        return;
    }

    struct lora_position pos;

    k_mutex_lock(&position_mutex, K_FOREVER);
    pos = current_position;
    k_mutex_unlock(&position_mutex);

    if (pos.valid && mqtt_is_connected())
    {
        char json_buf[64];
        int len = snprintf(json_buf, sizeof(json_buf), "{\"x\":%d,\"y\":%d}", pos.x, pos.y);

        if (len > 0 && len < (int)sizeof(json_buf))
        {
            int err = mqtt_publish_json(json_buf, len, MQTT_QOS_0_AT_MOST_ONCE);
            if (err)
            {
                LOG_WRN("Position publish failed: %d", err);
            }
            else
            {
                LOG_DBG("Published position: %s", json_buf);
            }
        }
    }

    k_work_reschedule(&position_publish_work, K_MSEC(POSITION_PUBLISH_INTERVAL_MS));
}

/* ------------ Console Input Thread ------------ */

static void print_calibration_help(void)
{
    printk("\n");
    printk("==============================================\n");
    printk("       MISOGATE CALIBRATION MODE\n");
    printk("==============================================\n");
    printk("\n");
    printk("Place the TBM at known positions and enter coordinates.\n");
    printk("\n");
    printk("Commands:\n");
    printk("  X Y     - Calibrate at position (X,Y) where X,Y are 0-1000\n");
    printk("            Example: '250 500' calibrates at (250, 500)\n");
    printk("  START   - Begin measurement mode and start MQTT publishing\n");
    printk("  STATUS  - Show current calibration points\n");
    printk("  CLEAR   - Clear all calibration points\n");
    printk("\n");
    printk("Minimum 2 calibration points required.\n");
    printk("==============================================\n");
    printk("\n");
    printk("> ");
}

static void print_calibration_status(void)
{
    printk("\nCalibration points: %d\n", g_calib_point_count);
    for (int i = 0; i < g_calib_point_count; i++)
    {
        struct calib_point *cp = &g_calib_points[i];
        printk("  Point %d: (%d, %d) -> ", i + 1, cp->x, cp->y);
        for (int nid = 1; nid <= MAX_NODES; nid++)
        {
            if (cp->node_valid[nid])
            {
                printk("N%d:%d ", nid, cp->node_absB[nid]);
            }
        }
        printk("\n");
    }
    printk("\n> ");
}

static void console_input_thread(void *p1, void *p2, void *p3)
{
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
    print_calibration_help();

    while (1)
    {
        char *line = console_getline();
        if (!line || strlen(line) == 0)
        {
            printk("> ");
            continue;
        }

        printk("Received: '%s'\n", line); /* Debug echo */

        /* Skip leading whitespace */
        char *cmd = line;
        while (*cmd && isspace((unsigned char)*cmd))
            cmd++;

        /* Convert to uppercase for comparison */
        char cmd_upper[64];
        int i;
        for (i = 0; cmd[i] && i < 63; i++)
        {
            cmd_upper[i] = toupper((unsigned char)cmd[i]);
        }
        cmd_upper[i] = '\0';

        if (strncmp(cmd_upper, "START", 5) == 0)
        {
            k_mutex_lock(&calib_mutex, K_FOREVER);
            if (g_calib_point_count < 2)
            {
                printk("Error: Need at least 2 calibration points!\n");
                printk("Current: %d points\n", g_calib_point_count);
            }
            else
            {
                printk("\n");
                printk("==============================================\n");
                printk("  STARTING MEASUREMENT MODE\n");
                printk("  %d calibration points loaded\n", g_calib_point_count);
                printk("  MQTT publishing enabled\n");
                printk("==============================================\n");
                printk("\n");

                g_calib_state = CALIB_STATE_RUNNING;
                g_mqtt_publish_enabled = true;
                g_current_calib_idx = -1;
            }
            k_mutex_unlock(&calib_mutex);

            if (g_calib_state == CALIB_STATE_RUNNING)
            {
                /* Exit calibration mode - don't print prompt */
                return;
            }
        }
        else if (strncmp(cmd_upper, "STATUS", 6) == 0)
        {
            k_mutex_lock(&calib_mutex, K_FOREVER);
            print_calibration_status();
            k_mutex_unlock(&calib_mutex);
        }
        else if (strncmp(cmd_upper, "CLEAR", 5) == 0)
        {
            k_mutex_lock(&calib_mutex, K_FOREVER);
            g_calib_point_count = 0;
            g_current_calib_idx = -1;
            memset(g_calib_points, 0, sizeof(g_calib_points));
            printk("Calibration points cleared.\n");
            k_mutex_unlock(&calib_mutex);
            printk("> ");
        }
        else
        {
            /* Try to parse as "X Y" */
            int x, y;
            if (sscanf(cmd, "%d %d", &x, &y) == 2)
            {
                if (x < 0 || x > 1000 || y < 0 || y > 1000)
                {
                    printk("Error: X and Y must be 0-1000\n");
                    printk("> ");
                }
                else if (g_calib_point_count >= MAX_CALIB_POINTS)
                {
                    printk("Error: Maximum calibration points reached (%d)\n",
                           MAX_CALIB_POINTS);
                    printk("> ");
                }
                else
                {
                    k_mutex_lock(&calib_mutex, K_FOREVER);

                    /* Store coordinates directly (0-1000) */
                    int idx = g_calib_point_count;
                    memset(&g_calib_points[idx], 0, sizeof(struct calib_point));
                    g_calib_points[idx].x = x;
                    g_calib_points[idx].y = y;
                    g_current_calib_idx = idx;
                    g_calib_point_count++;

                    printk("\nCalibrating point %d at (%d, %d)...\n",
                           idx + 1, x, y);
                    printk("Waiting for sensor readings...\n");
                    printk("(Need %d readings per node)\n\n",
                           CALIB_READINGS_PER_POINT);

                    k_mutex_unlock(&calib_mutex);

                    /* Wait for readings to be collected */
                    k_sleep(K_SECONDS(15));

                    printk("Calibration point %d recorded.\n", idx + 1);
                    print_calibration_status();
                }
            }
            else
            {
                printk("Unknown command: %s\n", cmd);
                printk("Enter 'X Y' coordinates or 'START'\n");
                printk("> ");
            }
        }
    }
}

/* ------------ LoRa Receiver Thread ------------ */

static void lora_receiver_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Wait for start signal */
    k_sem_take(&lora_start_sem, K_FOREVER);

    printk("LoRa receiver thread started\n");

    while (1)
    {
        uint8_t buf[64];
        int16_t rssi = 0;
        int8_t snr = 0;

        int len = lora_recv(lora_dev, buf, sizeof(buf),
                            K_SECONDS(10), &rssi, &snr);

        if (len > 0)
        {
            struct sensor_frame f;
            if (packet_parse_secure_frame_encmac(buf, (size_t)len, &f) == 0)
            {
                process_frame(&f, rssi, snr, len);
            }
            else
            {
                /* Only log security drops when running (not during calibration) */
                k_mutex_lock(&calib_mutex, K_FOREVER);
                calib_state_t state = g_calib_state;
                k_mutex_unlock(&calib_mutex);

                if (state == CALIB_STATE_RUNNING)
                {
                    LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
                }
            }
        }
        else if (len < 0 && len != -EAGAIN)
        {
            LOG_ERR("LoRa recv error: %d", len);
        }
    }
}

/* ------------ Public API ------------ */

int lora_receiver_init(void)
{
    /* Initialize node state */
    memset(g_nodes, 0, sizeof(g_nodes));
    memset(g_calib_points, 0, sizeof(g_calib_points));
    g_calib_point_count = 0;
    g_calib_state = CALIB_STATE_IDLE;

    lora_dev = DEVICE_DT_GET(LORA_NODE);
    if (!device_is_ready(lora_dev))
    {
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

    if (lora_config(lora_dev, &cfg) < 0)
    {
        LOG_ERR("lora_config failed");
        return -EIO;
    }

    LOG_INF("LoRa configured: %lu Hz, BW125, SF7, CR4/5",
            (unsigned long)LORA_FREQ_HZ);

    /* Create receiver thread (suspended until lora_receiver_start is called) */
    lora_thread_id = k_thread_create(&lora_thread_data,
                                     lora_stack,
                                     K_THREAD_STACK_SIZEOF(lora_stack),
                                     lora_receiver_thread,
                                     NULL, NULL, NULL,
                                     LORA_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(lora_thread_id, "lora_rx");

    /* Create console input thread */
    console_thread_id = k_thread_create(&console_thread_data,
                                        console_stack,
                                        K_THREAD_STACK_SIZEOF(console_stack),
                                        console_input_thread,
                                        NULL, NULL, NULL,
                                        CONSOLE_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(console_thread_id, "console_in");

    return 0;
}

void lora_receiver_start(void)
{
    printk("Starting LoRa receiver\n");
    k_sem_give(&lora_start_sem);

    /* Start position publishing at 500ms intervals (but won't actually publish
     * until g_mqtt_publish_enabled is true, which happens after calibration) */
    k_work_reschedule(&position_publish_work, K_MSEC(POSITION_PUBLISH_INTERVAL_MS));
}

void lora_start_calibration(void)
{
    printk("Starting calibration mode...\n");

    k_mutex_lock(&calib_mutex, K_FOREVER);
    g_calib_state = CALIB_STATE_WAITING_INPUT;
    g_mqtt_publish_enabled = false;
    k_mutex_unlock(&calib_mutex);

    /* Start console input thread */
    k_sem_give(&console_start_sem);
}

bool lora_is_running(void)
{
    bool running;
    k_mutex_lock(&calib_mutex, K_FOREVER);
    running = (g_calib_state == CALIB_STATE_RUNNING);
    k_mutex_unlock(&calib_mutex);
    return running;
}

int lora_get_position(struct lora_position *pos)
{
    if (!pos)
    {
        return -1;
    }

    k_mutex_lock(&position_mutex, K_FOREVER);
    *pos = current_position;
    k_mutex_unlock(&position_mutex);

    return pos->valid ? 0 : -1;
}

int lora_get_position_rel(void)
{
    return last_position_rel;
}

uint32_t lora_get_rx_count(void)
{
    return rx_ok_count;
}
