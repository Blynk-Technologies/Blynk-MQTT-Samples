#ifndef NETWORK_MANAGER_H_
#define NETWORK_MANAGER_H_

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

void network_manager_init(void);
bool is_network_connected(void);

#if defined(CONFIG_NET_DHCPV4)
void check_network_connection(struct k_work *work);
void l4_event_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface);
#endif

#endif /* NETWORK_MANAGER_H_ */

