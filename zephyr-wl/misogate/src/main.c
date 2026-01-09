#include <stdint.h>
#include <stdlib.h> // abs()
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/lora.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "packet.h"

LOG_MODULE_REGISTER(misogate, LOG_LEVEL_INF);

int packet_parse_secure_frame_encmac(const uint8_t *in, size_t in_len,
                                     struct sensor_frame *out);

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
      .tx = false,
      .iq_inverted = false,
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
        int16_t t_abs = abs(f.temp_c_times10 % 10);
        LOG_INF("SECURE PKT rx_ok=%u node=%u tx_seq=%u X=%u m-uT Y=%u m-uT "
                "Z=%u m-uT "
                "T=%d.%d C RSSI=%d dBm SNR=%d dB len=%d",
                (unsigned)rx_ok, (unsigned)f.node_id, (unsigned)f.tx_seq,
                (unsigned)f.x_uT_milli, (unsigned)f.y_uT_milli,
                (unsigned)f.z_uT_milli, f.temp_c_times10 / 10, t_abs, rssi, snr,
                len);
        /* TODO: emit JSON over UART to Nordic here */
      } else {
        LOG_WRN("SECURITY DROP len=%d RSSI=%d SNR=%d", len, rssi, snr);
      }
    } else {
      LOG_INF("misogate: waiting...");
    }
  }
}
