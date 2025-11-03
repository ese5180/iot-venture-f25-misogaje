#include "wifi.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(wifi, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <errno.h>

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback net_mgmt_cb;

static struct
{
    bool connected;
    bool connect_result;
    bool dhcp_bound;
} context;

/* -------------------- Wi-Fi event handling -------------------- */
static void handle_wifi_connect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;
    if (status->status)
    {
        LOG_ERR("Connection failed (%d)", status->status);
    }
    else
    {
        LOG_INF("Connected to WiFi");
        context.connected = true;
    }
    context.connect_result = true;
}

static void handle_wifi_disconnect_result(struct net_mgmt_event_callback *cb)
{
    const struct wifi_status *status = (const struct wifi_status *)cb->info;
    LOG_INF("Disconnected from WiFi (%d)", status->status);
    context.connected = false;
}

static void wifi_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                    uint32_t mgmt_event, struct net_if *iface)
{
    switch (mgmt_event)
    {
    case NET_EVENT_WIFI_CONNECT_RESULT:
        handle_wifi_connect_result(cb);
        break;
    case NET_EVENT_WIFI_DISCONNECT_RESULT:
        handle_wifi_disconnect_result(cb);
        break;
    default:
        break;
    }
}

/* -------------------- DHCP event handling -------------------- */
static void print_dhcp_ip(struct net_mgmt_event_callback *cb)
{
    const struct net_if_dhcpv4 *dhcpv4 = cb->info;
    const struct in_addr *addr = &dhcpv4->requested_ip;
    char dhcp_info[64];
    net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));
    LOG_INF("DHCP IP address: %s", dhcp_info);
}

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event, struct net_if *iface)
{
    if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND)
    {
        print_dhcp_ip(cb);
        context.dhcp_bound = true;
    }
}

/* -------------------- Public API -------------------- */
void wifi_init(void)
{
    net_mgmt_init_event_callback(&wifi_mgmt_cb, wifi_mgmt_event_handler, WIFI_MGMT_EVENTS);
    net_mgmt_add_event_callback(&wifi_mgmt_cb);
    net_mgmt_init_event_callback(&net_mgmt_cb, net_mgmt_event_handler, NET_EVENT_IPV4_DHCP_BOUND);
    net_mgmt_add_event_callback(&net_mgmt_cb);
}

int wifi_connect(void)
{
    struct net_if *iface = net_if_get_first_wifi();
    context.connected = false;
    context.connect_result = false;

    if (net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0))
    {
        LOG_ERR("Connection request failed");
        return -ENOEXEC;
    }
    LOG_INF("Connection requested");
    return 0;
}

bool wifi_is_connected(void)
{
    return context.connected;
}

bool wifi_is_dhcp_bound(void)
{
    return context.dhcp_bound;
}

int wifi_wait_for_connection(int timeout_ms)
{
    int elapsed = 0;
    const int sleep_interval = 100;

    while (!context.connect_result && elapsed < timeout_ms)
    {
        k_sleep(K_MSEC(sleep_interval));
        elapsed += sleep_interval;
    }

    if (!context.connect_result)
    {
        return -ETIMEDOUT;
    }

    if (!context.connected)
    {
        LOG_ERR("Failed to connect WiFi");
        return -ECONNREFUSED;
    }

    LOG_INF("WiFi connected successfully!");
    return 0;
}

int wifi_wait_for_dhcp(int timeout_ms)
{
    int elapsed = 0;
    const int sleep_interval = 100;

    LOG_INF("Waiting for DHCP...");

    while (!context.dhcp_bound && elapsed < timeout_ms)
    {
        k_sleep(K_MSEC(sleep_interval));
        elapsed += sleep_interval;
    }

    if (!context.dhcp_bound)
    {
        return -ETIMEDOUT;
    }

    LOG_INF("DHCP complete");
    return 0;
}

void wifi_print_mac_address(void)
{
    struct net_if *iface = net_if_get_first_wifi();
    if (iface == NULL)
    {
        LOG_ERR("No WiFi interface found");
        return;
    }

    struct net_linkaddr *link_addr = net_if_get_link_addr(iface);
    if (link_addr == NULL)
    {
        LOG_ERR("No link address found");
        return;
    }

    LOG_INF("WiFi MAC Address: %02x:%02x:%02x:%02x:%02x:%02x",
            link_addr->addr[0], link_addr->addr[1], link_addr->addr[2],
            link_addr->addr[3], link_addr->addr[4], link_addr->addr[5]);
}
