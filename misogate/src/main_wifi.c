#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sta, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#include <string.h>

#define WIFI_EVENTS ( \
    NET_EVENT_WIFI_SCAN_RESULT    | \
    NET_EVENT_WIFI_SCAN_DONE      | \
    NET_EVENT_WIFI_CONNECT_RESULT | \
    NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

static struct {
    bool scan_done;
    bool ssid_seen_any;
    bool have_24;
    uint16_t ch_24;
    int8_t rssi_24;
    uint8_t bssid_24[WIFI_MAC_ADDR_LEN];
    bool connect_result;
    bool connected;
} ctx;

/* -------- DHCP event handling -------- */
static void print_dhcp_ip(struct net_mgmt_event_callback *cb)
{
    const struct net_if_dhcpv4 *dhcpv4 = cb->info;
    char ip[32];
    net_addr_ntop(AF_INET, &dhcpv4->requested_ip, ip, sizeof ip);
    LOG_INF("DHCP IP address: %s", ip);
}

static void dhcp_event(struct net_mgmt_event_callback *cb,
                       uint32_t evt, struct net_if *iface)
{
    if (evt == NET_EVENT_IPV4_DHCP_BOUND) {
        print_dhcp_ip(cb);
    }
}

/* -------- Wi-Fi event handling -------- */
static void wifi_event(struct net_mgmt_event_callback *cb,
                       uint32_t evt, struct net_if *iface)
{
    switch (evt) {
    case NET_EVENT_WIFI_SCAN_RESULT: {
        const struct wifi_scan_result *r = cb->info;
        if (r->ssid_length &&
            strncmp(r->ssid, CONFIG_WIFI_CREDENTIALS_STATIC_SSID, r->ssid_length) == 0) {
            ctx.ssid_seen_any = true;

            /* Prefer best 2.4 GHz BSS (channels 1..14) */
            if (r->channel >= 1 && r->channel <= 14) {
                if (!ctx.have_24 || r->rssi > ctx.rssi_24) {
                    ctx.have_24 = true;
                    ctx.ch_24 = r->channel;
                    ctx.rssi_24 = r->rssi;
                    memcpy(ctx.bssid_24, r->mac, WIFI_MAC_ADDR_LEN);
                }
            }
            LOG_INF("Found SSID '%s' RSSI %d ch %u sec %u",
                    r->ssid, r->rssi, r->channel, r->security);
        }
        break;
    }
    case NET_EVENT_WIFI_SCAN_DONE:
        ctx.scan_done = true;
        LOG_INF("Scan done");
        break;

    case NET_EVENT_WIFI_CONNECT_RESULT: {
        const struct wifi_status *st = cb->info;
        if (st->status) {
            LOG_ERR("Connection failed (%d)", st->status);
        } else {
            LOG_INF("Connected to WiFi");
            ctx.connected = true;
        }
        ctx.connect_result = true;
        break;
    }

    case NET_EVENT_WIFI_DISCONNECT_RESULT: {
        const struct wifi_status *st = cb->info;
        LOG_INF("Disconnected (%d)", st->status);
        ctx.connected = false;
        break;
    }

    default:
        break;
    }
}

/* -------- Helpers -------- */
static int request_scan(struct net_if *iface)
{
    int rc = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
    if (rc) {
        LOG_ERR("Scan request failed (%d)", rc);
    } else {
        LOG_INF("Scan requested");
    }
    return rc;
}

static int connect_wpa2_psk(struct net_if *iface,
                            const uint8_t *bssid, uint16_t channel)
{
    struct wifi_connect_req_params cnx = {
        .ssid        = CONFIG_WIFI_CREDENTIALS_STATIC_SSID,
        .ssid_length = strlen(CONFIG_WIFI_CREDENTIALS_STATIC_SSID),
        .psk         = CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD,
        .psk_length  = strlen(CONFIG_WIFI_CREDENTIALS_STATIC_PASSWORD),
        .security    = WIFI_SECURITY_TYPE_PSK,   /* WPA2-PSK */
        .channel     = WIFI_CHANNEL_ANY,
    };

    /* Lock to a known-good 2.4 GHz BSSID + channel if we have them */
    if (channel >= 1 && channel <= 14) {
        cnx.channel = channel;
#ifdef WIFI_CONNECT_REQ_PARAMS_HAS_BSSID   /* guard if your headers differ */
        memcpy(cnx.bssid, bssid, WIFI_MAC_ADDR_LEN);
        cnx.bssid_set = true;
#endif
        LOG_INF("Locking to 2.4 GHz ch %u", channel);
    }

    /* Make PMF/802.11w optional if supported by this SDK */
#ifdef WIFI_MFP_OPTIONAL
    cnx.mfp = WIFI_MFP_OPTIONAL;
#endif

    int rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &cnx, sizeof(cnx));
    if (rc) {
        LOG_ERR("Connection request failed (%d)", rc);
        return rc;
    }
    LOG_INF("Connection requested");
    return 0;
}

/* -------- App entry -------- */
int main(void)
{
    LOG_INF("Wi-Fi bring-up");

    net_mgmt_init_event_callback(&wifi_cb, wifi_event, WIFI_EVENTS);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&dhcp_cb, dhcp_event, NET_EVENT_IPV4_DHCP_BOUND);
    net_mgmt_add_event_callback(&dhcp_cb);

    struct net_if *iface = net_if_get_first_wifi();
    if (!iface) {
        LOG_ERR("No Wi-Fi interface");
        return 0;
    }

    /* Scan to learn best 2.4 GHz BSSID + channel */
    ctx = (typeof(ctx)){0};
    if (!request_scan(iface)) {
        int waited = 0;
        while (!ctx.scan_done && waited < 10000) {
            k_sleep(K_MSEC(100));
            waited += 100;
        }
        if (!ctx.scan_done) {
            LOG_ERR("Timed out waiting for scan");
        } else if (!ctx.ssid_seen_any) {
            LOG_ERR("Target SSID '%s' not seen", CONFIG_WIFI_CREDENTIALS_STATIC_SSID);
        }
    }

    /* Connect, locked to 2.4 GHz BSSID if available */
    ctx.connect_result = ctx.connected = false;
    (void)connect_wpa2_psk(iface, ctx.have_24 ? ctx.bssid_24 : NULL,
                                  ctx.have_24 ? ctx.ch_24    : 0);

    int waited = 0;
    while (!ctx.connect_result && waited < 25000) {
        k_sleep(K_MSEC(100));
        waited += 100;
        if ((waited % 2000) == 0) {
            LOG_INF("Waiting for connect result... (%d ms)", waited);
        }
    }

    if (!ctx.connect_result) {
        LOG_ERR("Timed out waiting for connect result");
    } else if (ctx.connected) {
        LOG_INF("Wi-Fi connected; waiting for DHCP lease...");
        k_sleep(K_FOREVER);
    } else {
        LOG_ERR("Failed to connect");
    }

    return 0;
}
