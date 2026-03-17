/*
 * SPDX-FileCopyrightText: 2025 Khrystyna Olkhovetska for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_mgr, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>

#include "network_manager.h"
#include "mqtt_client.h"

static bool network_connected = false;

/* Work item for checking network connection */
static struct k_work_delayable check_network_conn;

bool is_network_connected(void)
{
    return network_connected;
}

void network_manager_init(void)
{
#if defined(CONFIG_NET_DHCPV4)
    k_work_init_delayable(&check_network_conn, check_network_connection);
    net_mgmt_init_event_callback(&l4_mgmt_cb,
                                 l4_event_handler,
                                 L4_EVENT_MASK);
    net_mgmt_add_event_callback(&l4_mgmt_cb);
    k_work_schedule(&check_network_conn, K_NO_WAIT);
#else
    /* For static IP, network is ready immediately */
    network_connected = true;
    k_sem_give(&mqtt_start);
#endif
}

#if defined(CONFIG_NET_DHCPV4)
void check_network_connection(struct k_work *work)
{
    struct net_if *iface;

    if (mqtt_connected) {
        return;
    }

    iface = net_if_get_default();
    if (!iface) {
        goto retry;
    }

    if (iface->config.dhcpv4.state == NET_DHCPV4_BOUND) {
        LOG_INF("Network connected - DHCP bound");
        network_connected = true;
        k_sem_give(&mqtt_start);
        return;
    }

retry:
    k_work_reschedule(&check_network_conn, K_SECONDS(3));
}

void l4_event_handler(struct net_mgmt_event_callback *cb,
                     uint32_t mgmt_event,
                     struct net_if *iface)
{
    if ((mgmt_event & L4_EVENT_MASK) != mgmt_event) {
        return;
    }

    if (mgmt_event == NET_EVENT_L4_CONNECTED) {
        LOG_INF("Network L4 connected");
        network_connected = true;
        k_work_reschedule(&check_network_conn, K_SECONDS(3));
        return;
    }

    if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
        LOG_INF("Network L4 disconnected");
        network_connected = false;
        abort_mqtt_connection();
        k_work_cancel_delayable(&check_network_conn);
        return;
    }
}
#endif

