
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_stats.h>
#include "wifi.h"
#include "mqtt.h"

/* -------------------- Main -------------------- */
int main(void)
{
    LOG_INF("Starting WiFi + MQTT demo");

    /* === Dynamic heap availability estimation === */
    size_t free_estimate = 0;
    size_t step = 1024; // Allocate in 1 KB chunks
    void *ptrs[64];
    int i = 0;

    for (i = 0; i < 64; i++)
    {
        ptrs[i] = k_malloc(step);
        if (ptrs[i] == NULL)
        {
            break;
        }
        free_estimate += step;
    }

    LOG_INF("Estimated free heap before MQTT: ~%zu bytes", free_estimate);

    for (int j = 0; j < i; j++)
    {
        k_free(ptrs[j]);
    }

    /* --- Network setup --- */
    wifi_init();

    k_sleep(K_SECONDS(1));

    if (wifi_connect() != 0)
    {
        LOG_ERR("Failed to request WiFi connection");
        return -1;
    }

    /* Wait for connection with timeout (30 seconds) */
    if (wifi_wait_for_connection(30000) != 0)
    {
        return -1;
    }

    wifi_print_mac_address();

    /* Wait for DHCP with timeout (30 seconds) */
    if (wifi_wait_for_dhcp(30000) != 0)
    {
        LOG_ERR("DHCP timeout");
        return -1;
    }

    LOG_INF("Testing network connectivity...");

    /* Test basic TCP connectivity to broker before trying MQTT */
    int test_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (test_sock < 0)
    {
        LOG_ERR("Failed to create test socket: %d", errno);
    }
    else
    {
        struct sockaddr_in test_addr;
        test_addr.sin_family = AF_INET;
        test_addr.sin_port = htons(CONFIG_MQTT_BROKER_PORT);
        inet_pton(AF_INET, CONFIG_MQTT_BROKER_ADDR, &test_addr.sin_addr);

        LOG_INF("Testing TCP connection to %s:%d...", CONFIG_MQTT_BROKER_ADDR, CONFIG_MQTT_BROKER_PORT);
        int conn_ret = connect(test_sock, (struct sockaddr *)&test_addr, sizeof(test_addr));
        if (conn_ret < 0)
        {
            LOG_ERR("TCP connection test failed: %d (errno: %d)", conn_ret, errno);
            LOG_ERR("This suggests network routing or firewall issue");
        }
        else
        {
            LOG_INF("TCP connection test SUCCEEDED!");
        }
        close(test_sock);
        k_sleep(K_MSEC(500));
    }

    LOG_INF("Starting MQTT connection...");

    if (mqtt_connect_broker() != 0)
    {
        LOG_ERR("Failed to connect to MQTT broker");
        return -1;
    }

    /* Wait a bit after connection before publishing */
    k_sleep(K_SECONDS(1));

    /* Publish initial message */
    const char *payload = "Hello from misogate!";
    mqtt_publish_message(CONFIG_MQTT_PUB_TOPIC, payload);

    /* Run main MQTT loop (this function runs indefinitely) */
    int ret = mqtt_run_loop();

    LOG_ERR("MQTT loop exited with error: %d", ret);
    mqtt_disconnect_broker();
    return ret;
}
