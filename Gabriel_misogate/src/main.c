#include <math.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "packet.h"

LOG_MODULE_REGISTER(misogate, LOG_LEVEL_INF);

/* --- Multi-node + relative position config --- */
#define MAX_NODES 4               /* we use node IDs 1 and 2 for now */
#define BASELINE_SAMPLES 20       /* packets per node to learn baseline */
#define POSITION_MIN_ANOM 2000.0f /* ignore if both anomalies tiny */

/* Per-node state at the gateway */
struct node_state {
  bool have_baseline;
  uint32_t baseline_count;
  int64_t baseline_sum_absB; /* sum of |B| during baseline learning */
  int32_t baseline_absB;     /* learned baseline |B| in m-uT */

  int32_t last_absB;  /* last |B| in m-uT */
  int32_t last_dAbsB; /* last anomaly |B|-baseline in m-uT */
  uint32_t last_seq;
};

static struct node_state g_nodes[MAX_NODES + 1];

/* Compute |B| from components in m-uT using float math */
static int32_t compute_absB_m_uT(int32_t x, int32_t y, int32_t z) {
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
static void update_node_state(const struct sensor_frame *f, int32_t absB) {
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
      LOG_INF("Node %u baseline learned: |B| ≈ %d m-uT", nid,
              ns->baseline_absB);
    }
  }

  ns->last_absB = absB;
  ns->last_seq = f->tx_seq;

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
static int estimate_position_rel_0_100(void) {
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
  if (ratio < 0.0f)
    ratio = 0.0f;
  if (ratio > 1.0f)
    ratio = 1.0f;

  int pos_rel =
      (int)(ratio * 100.0f + 0.5f); /* round to nearest int in [0,100] */
  if (pos_rel < 0)
    pos_rel = 0;
  if (pos_rel > 100)
    pos_rel = 100;
  return pos_rel;
}

void main(void) {
  const struct device *lora = DEVICE_DT_GET(DT_ALIAS(lora0));
  if (!device_is_ready(lora)) {
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
  if (lora_config(lora, &cfg) < 0) {
    LOG_ERR("lora_config failed");
    return;
  }

  LOG_INF(
      "misogate: RX (Encrypt-then-MAC, multi-node, relative position 0..100)");

  memset(g_nodes, 0, sizeof(g_nodes));

  uint32_t rx_ok = 0;

  while (1) {
    uint8_t buf[64];
    int16_t rssi = 0, snr = 0;
    int len = lora_recv(lora, buf, sizeof(buf), K_SECONDS(10), &rssi, &snr);

    if (len > 0) {
      struct sensor_frame f;
      if (packet_parse_secure_frame_encmac(buf, len, &f) == 0) {
        rx_ok++;

        /* Compute |B| and update per-node state */
        int32_t absB =
            compute_absB_m_uT(f.x_uT_milli, f.y_uT_milli, f.z_uT_milli);

        update_node_state(&f, absB);

        /* pretty temperature */
        int16_t t_abs = abs(f.temp_c_times10 % 10);

        struct node_state *ns =
            (f.node_id <= MAX_NODES) ? &g_nodes[f.node_id] : NULL;

        int32_t dAbs = (ns && ns->have_baseline) ? ns->last_dAbsB : 0;

        LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u "
                "X=%d m-uT Y=%d m-uT Z=%d m-uT |B|=%d m-uT d|B|=%d m-uT "
                "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                (unsigned)rx_ok, (unsigned)f.node_id, (unsigned)f.tx_seq,
                f.x_uT_milli, f.y_uT_milli, f.z_uT_milli, absB, dAbs,
                f.temp_c_times10 / 10, t_abs, rssi, snr, len);

        /* Try to compute a relative position 0..100 using nodes 1 and 2 */
        int pos_rel = estimate_position_rel_0_100();
        if (pos_rel >= 0) {
          /* This is the line your teammate can parse to drive the slider */
          struct node_state *n1 = &g_nodes[1];
          struct node_state *n2 = &g_nodes[2];

          float d0 = fabsf((float)n1->last_dAbsB);
          float d1 = fabsf((float)n2->last_dAbsB);

          LOG_INF("POS_REL node1-2: %d (0=node1,100=node2) "
                  "d0=%.0f m-uT d1=%.0f m-uT",
                  pos_rel, (double)d0, (double)d1);
        } else {
          LOG_INF("POS_REL node1-2: N/A (baselines not ready or anomalies too "
                  "small)");
        }

      } else {
        LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
      }
    } else {
      LOG_INF("misogate: waiting...");
    }
  }
}
