#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h> // abs()

#include "packet.h"

#include <math.h>

#define MAX_NODES           16
#define BASELINE_SAMPLES    20

static float   g_baseline_mag[MAX_NODES];
static uint32_t g_baseline_cnt[MAX_NODES];
static bool    g_baseline_ready[MAX_NODES];

LOG_MODULE_REGISTER(misogate, LOG_LEVEL_INF);

int packet_parse_secure_frame_encmac(const uint8_t *in, size_t in_len, struct sensor_frame *out);

static float compute_mag_uT_milli(const struct sensor_frame *f)
{
    // components are already in m-uT (µT * 1000)
    double x = (double)f->x_uT_milli;
    double y = (double)f->y_uT_milli;
    double z = (double)f->z_uT_milli;

    double mag = sqrt(x*x + y*y + z*z);  // still in m-uT
    return (float)mag;
}

static void update_baseline(uint8_t node_id, float mag)
{
    if (node_id == 0 || node_id >= MAX_NODES) {
        return; // ignore out-of-range IDs
    }

    uint8_t idx = node_id;  // 1..MAX_NODES

    if (!g_baseline_ready[idx]) {
        // simple running average over first BASELINE_SAMPLES packets
        uint32_t n = g_baseline_cnt[idx];
        if (n < BASELINE_SAMPLES) {
            g_baseline_cnt[idx] = n + 1;
            g_baseline_mag[idx] = (g_baseline_mag[idx] * n + mag) / (float)(n + 1);
            if (g_baseline_cnt[idx] == BASELINE_SAMPLES) {
                g_baseline_ready[idx] = true;
                LOG_INF("Node %u baseline learned: |B| ≈ %d m-uT",
                        (unsigned)node_id,
                        (int)lroundf(g_baseline_mag[idx]));
            }
        }
    }
}

void main(void)
{
    const struct device *lora = DEVICE_DT_GET(DT_ALIAS(lora0));
    if (!device_is_ready(lora)) {
        LOG_ERR("LoRa device not ready");
        return;
    }

    struct lora_modem_config cfg = {
        .frequency      = 915000000UL,
        .bandwidth      = BW_125_KHZ,
        .datarate       = SF_7,
        .coding_rate    = CR_4_5,
        .preamble_len   = 8,
        .tx_power       = 10,
        .tx             = false,   /* RX */
        .iq_inverted    = false,
        .public_network = true,
    };

    if (lora_config(lora, &cfg) < 0) {
        LOG_ERR("lora_config failed");
        return;
    }

    LOG_INF("misogate: RX (Encrypt-then-MAC)");

    uint32_t rx_ok = 0;

    while (1) {
        uint8_t buf[64];
        int16_t rssi, snr;
        int len = lora_recv(lora, buf, sizeof(buf), K_SECONDS(10), &rssi, &snr);

        if (len > 0) {
            struct sensor_frame f;

            if (packet_parse_secure_frame_encmac(buf, len, &f) == 0) {
                rx_ok++;

                /* 1) compute |B| in m-uT */
                float mag = compute_mag_uT_milli(&f);

                /* 2) update / use per-node baseline */
                update_baseline(f.node_id, mag);

                float dmag = 0.0f;
                bool have_baseline = false;

                if (f.node_id < MAX_NODES && g_baseline_ready[f.node_id]) {
                    have_baseline = true;
                    dmag = mag - g_baseline_mag[f.node_id];
                }

                int16_t t_abs = abs(f.temp_c_times10 % 10);

                if (have_baseline) {
                    LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                            "X=%d m-uT Y=%d m-uT Z=%d m-uT |B|=%d m-uT d|B|=%d m-uT "
                            "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                            (unsigned)rx_ok,
                            (unsigned)f.node_id,
                            (unsigned)f.tx_seq,
                            f.x_uT_milli,
                            f.y_uT_milli,
                            f.z_uT_milli,
                            (int)(mag + 0.5f),
                            (int)(dmag >= 0 ? dmag + 0.5f : dmag - 0.5f),
                            f.temp_c_times10 / 10,
                            t_abs,
                            rssi,
                            snr,
                            len);
                } else {
                    LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                            "X=%d m-uT Y=%d m-uT Z=%d m-uT |B|=%d m-uT "
                            "(baseline learning %u/%u) "
                            "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                            (unsigned)rx_ok,
                            (unsigned)f.node_id,
                            (unsigned)f.tx_seq,
                            f.x_uT_milli,
                            f.y_uT_milli,
                            f.z_uT_milli,
                            (int)(mag + 0.5f),
                            (unsigned)g_baseline_cnt[f.node_id],
                            (unsigned)BASELINE_SAMPLES,
                            f.temp_c_times10 / 10,
                            t_abs,
                            rssi,
                            snr,
                            len);
                }

            } else {
                LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
            }
        } else {
            LOG_INF("misogate: waiting...");
        }
    }
}
