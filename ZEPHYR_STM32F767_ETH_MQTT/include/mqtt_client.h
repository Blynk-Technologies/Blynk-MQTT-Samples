#ifndef MQTT_CLIENT_H_
#define MQTT_CLIENT_H_

#include <zephyr/net/mqtt.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_mgmt.h>
#include <stdbool.h>

/* Constants */
#define APP_CA_CERT_TAG         1
#define APP_MQTT_BUFFER_SIZE   1024

/* Global variables for MQTT connection state */
extern struct net_mgmt_event_callback l4_mgmt_cb;
extern const uint32_t L4_EVENT_MASK;
extern struct mqtt_client client_ctx;
extern bool mqtt_connected;
extern struct k_sem mqtt_start;

/* Broker configuration */
extern char broker_host[128];
extern uint16_t broker_port;

/* Core MQTT functions - communication only */
void connect_to_cloud_and_publish(void);
void publish_str(const char *topic, const char *value);
void terminal_print(const char *msg);
void abort_mqtt_connection(void);

/* Internal MQTT functions */
void client_init(struct mqtt_client *client);

/* Business logic callbacks - implemented in main.c */
extern void handle_incoming_topic(const char *topic, const char *payload);
extern void on_mqtt_connected(void);
extern void on_mqtt_subscribed(void);
extern void on_mqtt_disconnected(void);

#endif /* MQTT_CLIENT_H_ */

