#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sta, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>

#include "net_private.h"

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | \
                          NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback wifi_mgmt_cb;
static struct net_mgmt_event_callback net_mgmt_cb;

static struct
{
        bool connected;
        bool connect_result;
} context;

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

static void print_dhcp_ip(struct net_mgmt_event_callback *cb)
{
        const struct net_if_dhcpv4 *dhcpv4 = cb->info;
        const struct in_addr *addr = &dhcpv4->requested_ip;
        char dhcp_info[128];

        net_addr_ntop(AF_INET, addr, dhcp_info, sizeof(dhcp_info));
        LOG_INF("DHCP IP address: %s", dhcp_info);
}

static void net_mgmt_event_handler(struct net_mgmt_event_callback *cb,
                                   uint32_t mgmt_event, struct net_if *iface)
{
        switch (mgmt_event)
        {
        case NET_EVENT_IPV4_DHCP_BOUND:
                print_dhcp_ip(cb);
                break;
        default:
                break;
        }
}

static int wifi_connect(void)
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

int main(void)
{
        LOG_INF("Starting WiFi connection");

        /* Initialize WiFi management callbacks */
        net_mgmt_init_event_callback(&wifi_mgmt_cb,
                                     wifi_mgmt_event_handler,
                                     WIFI_MGMT_EVENTS);
        net_mgmt_add_event_callback(&wifi_mgmt_cb);

        /* Initialize network management callbacks for DHCP */
        net_mgmt_init_event_callback(&net_mgmt_cb,
                                     net_mgmt_event_handler,
                                     NET_EVENT_IPV4_DHCP_BOUND);
        net_mgmt_add_event_callback(&net_mgmt_cb);

        /* Wait for WiFi subsystem to initialize */
        k_sleep(K_SECONDS(1));

        /* Connect to WiFi using stored credentials */
        wifi_connect();

        /* Wait for connection result */
        while (!context.connect_result)
        {
                k_sleep(K_MSEC(100));
        }

        if (context.connected)
        {
                LOG_INF("WiFi connected successfully");
                /* Keep running */
                k_sleep(K_FOREVER);
        }
        else
        {
                LOG_ERR("Failed to connect to WiFi");
        }

        return 0;
}
