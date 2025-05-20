/*
 * SPDX-FileCopyrightText: 2025 Khrystyna Olkhovetska for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mqtt, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "mqtt_client.h"
#include "root_certificates.h"

/* MQTT buffers */
static uint8_t rx_buffer[APP_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[APP_MQTT_BUFFER_SIZE];

/* MQTT connection variables */
struct mqtt_client client_ctx;
bool mqtt_connected = false;
struct k_sem mqtt_start = Z_SEM_INITIALIZER(mqtt_start, 0, 1);

/* Network variables */
struct net_mgmt_event_callback l4_mgmt_cb;
const uint32_t L4_EVENT_MASK = (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED);

/* Socket handling */
static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];
static int nfds;

/* MQTT subscription */
static uint8_t devbound_topic[] = "downlink/#";
static struct mqtt_topic subs_topic;
static struct mqtt_subscription_list subs_list;

/* Broker configuration */
char broker_host[128] = CONFIG_CLOUD_BLYNK_SERVER_ADDR;
uint16_t broker_port   = CONFIG_CLOUD_BLYNK_SERVER_PORT;

/* Constants */
#define TLS_SNI_HOSTNAME        CONFIG_CLOUD_BLYNK_SERVER_ADDR
#define APP_SLEEP_MSECS         8000
#define MQTT_CLIENTID           ""

/* TLS configuration */
static const sec_tag_t m_sec_tags[] = { APP_CA_CERT_TAG };

#if defined(CONFIG_DNS_RESOLVER)
static struct zsock_addrinfo hints;
static struct zsock_addrinfo *haddr;
#endif

/* External business logic callbacks - implemented in main.c */
extern void handle_incoming_topic(const char *topic, const char *payload);
extern void on_mqtt_connected(void);
extern void on_mqtt_subscribed(void);
extern void on_mqtt_disconnected(void);

/* Simplified FD handling */
static void prepare_fds(struct mqtt_client *client)
{
    if (client->transport.type == MQTT_TRANSPORT_SECURE) {
        fds[0].fd = client->transport.tls.sock;
    } else {
        fds[0].fd = client->transport.tcp.sock;
    }
    fds[0].events = ZSOCK_POLLIN;
    nfds = 1;
}

static void clear_fds(void)
{
    nfds = 0;
}

static int wait(int timeout)
{
    if (nfds <= 0) {
        return -EINVAL;
    }
    return zsock_poll(fds, nfds, timeout);
}

/* Broker initialization */
static void broker_init(void)
{
    struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;
    broker4->sin_family = AF_INET;
    broker4->sin_port   = htons(broker_port);

#if defined(CONFIG_DNS_RESOLVER)
    if (haddr != NULL) {
        broker4->sin_addr = ((struct sockaddr_in *)(haddr->ai_addr))->sin_addr;
    } else {
        memset(broker4, 0, sizeof(*broker4));
    }
#else
    if (zsock_inet_pton(AF_INET, broker_host, &broker4->sin_addr) != 1) {
        memset(broker4, 0, sizeof(*broker4));
    }
#endif
}

/* MQTT subscription */
static void subscribe(struct mqtt_client *client)
{
    int err;

    subs_topic.topic.utf8   = devbound_topic;
    subs_topic.topic.size   = strlen(devbound_topic);
    subs_list.list          = &subs_topic;
    subs_list.list_count    = 1U;
    subs_list.message_id    = 1U;

    err = mqtt_subscribe(client, &subs_list);
    if (err) {
        LOG_ERR("Failed to subscribe to %s", devbound_topic);
    }
}

/* Publish functions */
void publish_str(const char *topic, const char *value)
{
    if (!mqtt_connected) {
        return;
    }

    struct mqtt_publish_param param = {
        .message.topic.qos        = MQTT_QOS_1_AT_LEAST_ONCE,
        .message.topic.topic.utf8 = (uint8_t *)topic,
        .message.topic.topic.size = strlen(topic),
        .message.payload.data     = (void *)value,
        .message.payload.len      = strlen(value),
        .message_id               = sys_rand16_get(),
        .dup_flag                 = 0U,
        .retain_flag              = 0U,
    };

    int err = mqtt_publish(&client_ctx, &param);
    if (err) {
        LOG_ERR("Failed to publish to %s (err %d)", topic, err);
    }
}

void terminal_print(const char *msg)
{
    char payload[128];
    snprintf(payload, sizeof(payload), "%s\n", msg);
    publish_str("ds/Terminal", payload);
}

static void mqtt_event_handler(struct mqtt_client *const client,
                              const struct mqtt_evt *evt)
{
    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result) {
            LOG_ERR("MQTT connect failed: %d", evt->result);
            break;
        }

        mqtt_connected = true;
        LOG_INF("MQTT connected");

        /* Call business logic callback */
        on_mqtt_connected();
        break;

    case MQTT_EVT_PUBLISH: {
        int total_len = evt->param.publish.message.payload.len;
        if (total_len >= 512) {
            LOG_ERR("Payload too large: %d bytes", total_len);
            return;
        }

        uint8_t payload_buf[512];
        int read_so_far = 0;
        int rc;

        while (read_so_far < total_len) {
            rc = mqtt_read_publish_payload(client,
                                          payload_buf + read_so_far,
                                          total_len - read_so_far);
            if (rc < 0) {
                LOG_ERR("Failed to read payload: %d", rc);
                return;
            }
            read_so_far += rc;
        }
        payload_buf[total_len] = '\0';

        /* Extract topic */
        char topic_str[128];
        int topic_len = evt->param.publish.message.topic.topic.size;
        if (topic_len >= (int)sizeof(topic_str)) {
            topic_len = sizeof(topic_str) - 1;
        }
        memcpy(topic_str,
               evt->param.publish.message.topic.topic.utf8,
               topic_len);
        topic_str[topic_len] = '\0';

        /* Call business logic handler */
        handle_incoming_topic(topic_str, (char *)payload_buf);

        /* Send ACK */
        struct mqtt_puback_param puback = {
            .message_id = evt->param.publish.message_id
        };
        mqtt_publish_qos1_ack(client, &puback);
        break;
    }

    case MQTT_EVT_DISCONNECT:
        mqtt_connected = false;
        LOG_INF("MQTT disconnected: %d", evt->result);
        clear_fds();

        /* Call business logic callback */
        on_mqtt_disconnected();
        break;

    case MQTT_EVT_SUBACK:
        LOG_INF("Subscribed successfully");

        /* Call business logic callback */
        on_mqtt_subscribed();
        break;

    default:
        LOG_DBG("Unhandled MQTT event: %d", evt->type);
        break;
    }
}

void client_init(struct mqtt_client *client)
{
    static struct mqtt_utf8 password;
    static struct mqtt_utf8 username;
    struct mqtt_sec_config *tls_config;

    mqtt_client_init(client);
    broker_init();

    /* Basic configuration */
    client->broker = &broker;
    client->evt_cb = mqtt_event_handler;
    client->client_id.utf8 = (uint8_t *)MQTT_CLIENTID;
    client->client_id.size = strlen(MQTT_CLIENTID);

    /* Credentials */
    password.utf8 = (uint8_t *)CONFIG_CLOUD_BLYNK_AUTH_TOKEN;
    password.size = strlen(CONFIG_CLOUD_BLYNK_AUTH_TOKEN);
    client->password = &password;

    username.utf8 = (uint8_t *)"device";
    username.size = strlen("device");
    client->user_name = &username;

    client->protocol_version = MQTT_VERSION_3_1_1;

    /* Buffers */
    client->rx_buf      = rx_buffer;
    client->rx_buf_size = sizeof(rx_buffer);
    client->tx_buf      = tx_buffer;
    client->tx_buf_size = sizeof(tx_buffer);

    /* TLS configuration */
    client->transport.type = MQTT_TRANSPORT_SECURE;
    tls_config = &client->transport.tls.config;
    tls_config->peer_verify = TLS_PEER_VERIFY_REQUIRED;
    tls_config->cipher_list = NULL;
    tls_config->sec_tag_list = m_sec_tags;
    tls_config->sec_tag_count = ARRAY_SIZE(m_sec_tags);
    tls_config->hostname = TLS_SNI_HOSTNAME;
}

static void poll_mqtt(void)
{
    int rc;

    while (mqtt_connected) {
        rc = wait(SYS_FOREVER_MS);
        if (rc > 0) {
            if (mqtt_input(&client_ctx) != 0) {
                LOG_WRN("mqtt_input failed, dropping connection");
                mqtt_connected = false;
                break;
            }
        }
    }
}

static int try_to_connect(struct mqtt_client *client)
{
    uint8_t retries = 3U;
    int rc;

    LOG_INF("Connecting to %s:%d", broker_host, broker_port);

    while (retries--) {
        client_init(client);

        rc = mqtt_connect(client);
        if (rc) {
            LOG_ERR("MQTT connect failed (rc=%d, retries left=%d)", rc, retries);
            if ((rc == 4) || (rc == 5)) {
                LOG_ERR("Invalid BLYNK_AUTH_TOKEN");
            }
            k_sleep(K_SECONDS(10));
            continue;
        }

        prepare_fds(client);
        rc = wait(APP_SLEEP_MSECS);
        if (rc < 0) {
            mqtt_abort(client);
            return rc;
        }

        mqtt_input(client);
        if (mqtt_connected) {
            subscribe(client);
            return 0;
        }

        mqtt_abort(client);
        k_sleep(K_SECONDS(10));
    }

    return -EINVAL;
}

/* DNS resolution */
#if defined(CONFIG_DNS_RESOLVER)
static int get_mqtt_broker_addrinfo(void)
{
    int retries = 3;
    int rc = -EINVAL;
    char port_str[6];

    snprintf(port_str, sizeof(port_str), "%u", broker_port);

    while (retries--) {
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = 0;

        rc = zsock_getaddrinfo(broker_host, port_str, &hints, &haddr);
        if (rc == 0) {
            return 0;
        }

        LOG_WRN("DNS resolution failed, retrying...");
        k_sleep(K_SECONDS(1));
    }

    return rc;
}
#endif

/* Main connection function */
void connect_to_cloud_and_publish(void)
{
    int rc = -EINVAL;

#if defined(CONFIG_DNS_RESOLVER)
    rc = get_mqtt_broker_addrinfo();
    if (rc) {
        LOG_ERR("Cannot resolve %s:%u (%d)", broker_host, broker_port, rc);
        return;
    }
#endif

    rc = try_to_connect(&client_ctx);
    if (rc) {
        LOG_ERR("MQTT connect failed: %d", rc);
        return;
    }

    poll_mqtt();
}

/* Abort connection helper */
void abort_mqtt_connection(void)
{
    if (mqtt_connected) {
        mqtt_connected = false;
        mqtt_abort(&client_ctx);
        clear_fds();
    }
}

