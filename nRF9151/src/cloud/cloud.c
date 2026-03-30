/*
 * Copyright (c) 2020 Circuit Dojo LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(cloud);

#include <date_time.h>
#include <modem/modem_key_mgmt.h>
#include <net/fota_download.h>
#include <cJSON.h>

#include "cloud.h"
#include "../credentials/credentials.h"

/* CA Certificate (ISRG Root X1 – covers both the MQTT broker and OTA downloads) */
static const char cert[] = {
#include "isrg-root-x1.pem"
};

/* ── MQTT configuration ──────────────────────────────────────────────────── */

#define MQTT_BROKER_PORT        8883
#define MQTT_KEEPALIVE_S        600   /* 10 minutes */
#define MQTT_CONNECT_TIMEOUT_MS 10000
#define RECONNECT_DELAY_S       10

/* ── Buffers ─────────────────────────────────────────────────────────────── */

#define MQTT_RX_BUF_SIZE  512
#define MQTT_TX_BUF_SIZE  256
#define PAYLOAD_BUF_SIZE  512
#define BROKER_HOST_SIZE   64

static uint8_t mqtt_rx_buf[MQTT_RX_BUF_SIZE];
static uint8_t mqtt_tx_buf[MQTT_TX_BUF_SIZE];
static uint8_t payload_buf[PAYLOAD_BUF_SIZE];

/* ── State ───────────────────────────────────────────────────────────────── */

static struct mqtt_client     client;
static struct sockaddr_storage broker_storage;

static char current_host[BROKER_HOST_SIZE];
static char redirect_host[BROKER_HOST_SIZE];
static bool redirect_pending;
static bool mqtt_connected;
static bool fota_in_progress;
static uint16_t msg_id_counter = 1;

/* ── Mutex – protects MQTT client from concurrent poll/publish access ────── */

K_MUTEX_DEFINE(mqtt_mutex);

/* ── Semaphore – signals main thread when first MQTT connection is up ─────── */

K_SEM_DEFINE(cloud_connected_sem, 0, 1);
static bool first_connect = true;

/* ── Poll thread ─────────────────────────────────────────────────────────── */

#define POLL_THREAD_STACK_SIZE  2048
#define POLL_THREAD_PRIORITY    5

K_THREAD_STACK_DEFINE(poll_stack, POLL_THREAD_STACK_SIZE);
static struct k_thread poll_thread;

/* ── Callback ────────────────────────────────────────────────────────────── */

static void (*cloud_callback)(struct device_data *data);

/* ── TLS sec tag ─────────────────────────────────────────────────────────── */

static sec_tag_t tls_sec_tags[] = { CONFIG_CLOUD_TLS_SEC_TAG };

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static uint16_t next_msg_id(void)
{
	if (msg_id_counter == 0) {
		msg_id_counter = 1;
	}
	return msg_id_counter++;
}

/**
 * @brief Split a URL ("https://host/path") into host and path.
 *        Host is copied into caller's buffer; *path_out points into url.
 */
static int parse_url(const char *url, char *host, size_t host_sz,
		      const char **path_out)
{
	const char *p = url;

	if (strncmp(p, "https://", 8) == 0) {
		p += 8;
	} else if (strncmp(p, "http://", 7) == 0) {
		p += 7;
	}

	const char *slash = strchr(p, '/');

	if (!slash) {
		strncpy(host, p, host_sz - 1);
		host[host_sz - 1] = '\0';
		*path_out = "/";
	} else {
		size_t host_len = (size_t)(slash - p);

		if (host_len >= host_sz) {
			return -EINVAL;
		}
		memcpy(host, p, host_len);
		host[host_len] = '\0';
		*path_out = slash;
	}
	return 0;
}

/* ── FOTA ────────────────────────────────────────────────────────────────── */

static void fota_callback(const struct fota_download_evt *evt)
{
	switch (evt->id) {
	case FOTA_DOWNLOAD_EVT_PROGRESS:
		LOG_INF("OTA progress: %d%%", evt->progress);
		break;

	case FOTA_DOWNLOAD_EVT_FINISHED:
		LOG_INF("OTA download complete – rebooting to apply update");
		sys_reboot(SYS_REBOOT_COLD);
		break;

	case FOTA_DOWNLOAD_EVT_ERROR:
		LOG_ERR("OTA download error (cause %d)", evt->cause);
		fota_in_progress = false;
		break;

	case FOTA_DOWNLOAD_EVT_CANCELLED:
		LOG_WRN("OTA download cancelled");
		fota_in_progress = false;
		break;

	default:
		break;
	}
}

static void handle_ota(const uint8_t *payload, size_t len)
{
	char url[256] = {0};
	char ota_host[BROKER_HOST_SIZE];
	const char *ota_path;

	LOG_INF("OTA notification received");

	/* Try JSON first: {"url":"https://..."} */
	cJSON *root = cJSON_ParseWithLength((const char *)payload, len);

	if (root) {
		cJSON *url_item = cJSON_GetObjectItem(root, "url");

		if (cJSON_IsString(url_item)) {
			strncpy(url, url_item->valuestring, sizeof(url) - 1);
		}
		cJSON_Delete(root);
	}

	/* Fall back to treating the whole payload as a plain URL */
	if (url[0] == '\0') {
		size_t copy_len = (len < sizeof(url) - 1) ? len : sizeof(url) - 1;

		memcpy(url, payload, copy_len);
		url[copy_len] = '\0';
	}

	if (url[0] == '\0') {
		LOG_ERR("No OTA URL found in payload");
		return;
	}

	LOG_INF("OTA URL: %s", url);

	if (parse_url(url, ota_host, sizeof(ota_host), &ota_path) != 0) {
		LOG_ERR("Failed to parse OTA URL");
		return;
	}

	/* fota_download_start prepends '/' itself, so strip it from the path */
	const char *path = (ota_path[0] == '/') ? ota_path + 1 : ota_path;

	/* Prefix host with https:// so the downloader doesn't have to guess */
	char ota_host_https[BROKER_HOST_SIZE + sizeof("https://")];

	snprintk(ota_host_https, sizeof(ota_host_https), "https://%s", ota_host);

	fota_in_progress = true;
	int err = fota_download_start(ota_host_https, path,
				      CONFIG_CLOUD_TLS_SEC_TAG, 0, 0);
	if (err) {
		LOG_ERR("fota_download_start() failed: %d", err);
		fota_in_progress = false;
	}
}

/* ── Redirect ────────────────────────────────────────────────────────────── */

static void handle_redirect(const uint8_t *payload, size_t len)
{
	if (len == 0 || len >= BROKER_HOST_SIZE) {
		LOG_ERR("Invalid redirect payload (len=%zu)", len);
		return;
	}

	char raw[BROKER_HOST_SIZE];
	memcpy(raw, payload, len);
	raw[len] = '\0';

	/* Strip scheme (e.g. "mqtts://") */
	const char *host = raw;
	const char *scheme_end = strstr(raw, "://");
	if (scheme_end) {
		host = scheme_end + 3;
	}

	/* Strip port (e.g. ":8883") */
	char *colon = strchr(host, ':');
	if (colon) {
		*colon = '\0';
	}

	strncpy(redirect_host, host, BROKER_HOST_SIZE - 1);
	redirect_host[BROKER_HOST_SIZE - 1] = '\0';

	LOG_INF("Broker redirect requested → %s", redirect_host);
	redirect_pending = true;
}

/* ── Downlink virtual-pin handler ────────────────────────────────────────── */

static void handle_ds_downlink(const char *topic, size_t topic_len,
			       const uint8_t *payload, size_t payload_len)
{
	LOG_INF("Downlink DS topic=%.*s value=%.*s",
		(int)topic_len, topic, (int)payload_len, payload);

	if (cloud_callback) {
		struct device_data data = { .do_something = true };

		cloud_callback(&data);
	}
}

/* ── Device info publish ─────────────────────────────────────────────────── */

static void publish_device_info(void)
{
	cJSON *root = cJSON_CreateObject();

	if (!root) {
		LOG_ERR("Failed to create device info JSON");
		return;
	}

	cJSON_AddStringToObject(root, "tmpl",   credentials_get_template_id());
	cJSON_AddStringToObject(root, "ver",    CONFIG_FIRMWARE_VERSION);
	cJSON_AddStringToObject(root, "build",  __DATE__ " " __TIME__);
	cJSON_AddStringToObject(root, "type",   credentials_get_template_id());
	cJSON_AddNumberToObject(root, "rxbuff", MQTT_RX_BUF_SIZE);

	char *json = cJSON_PrintUnformatted(root);

	cJSON_Delete(root);

	if (!json) {
		LOG_ERR("Failed to serialise device info");
		return;
	}

	static const char topic[] = "info/mcu";

	struct mqtt_publish_param pub = {
		.message = {
			.topic = {
				.topic = { .utf8 = (uint8_t *)topic,
					   .size = sizeof(topic) - 1 },
				.qos   = MQTT_QOS_0_AT_MOST_ONCE,
			},
			.payload = {
				.data = (uint8_t *)json,
				.len  = strlen(json),
			},
		},
		.message_id  = 0,
		.dup_flag    = 0,
		.retain_flag = 0,
	};

	LOG_INF("PUBLISH → %s", topic);
	int err = mqtt_publish(&client, &pub);

	if (err) {
		LOG_ERR("Device info publish failed: %d", err);
	}

	cJSON_free(json);
}

/* ── MQTT subscriptions ──────────────────────────────────────────────────── */

static int subscribe_topics(void)
{
	static const struct mqtt_topic topics[] = {
		{
			.topic = { .utf8 = (uint8_t *)"downlink/redirect",
				   .size = sizeof("downlink/redirect") - 1 },
			.qos   = MQTT_QOS_0_AT_MOST_ONCE,
		},
		{
			.topic = { .utf8 = (uint8_t *)"downlink/ota/json",
				   .size = sizeof("downlink/ota/json") - 1 },
			.qos   = MQTT_QOS_0_AT_MOST_ONCE,
		},
		{
			.topic = { .utf8 = (uint8_t *)"downlink/ds/#",
				   .size = sizeof("downlink/ds/#") - 1 },
			.qos   = MQTT_QOS_0_AT_MOST_ONCE,
		},
	};

	const struct mqtt_subscription_list sub_list = {
		.list       = (struct mqtt_topic *)topics,
		.list_count = ARRAY_SIZE(topics),
		.message_id = next_msg_id(),
	};

	LOG_INF("Subscribing to Blynk downlink topics");
	return mqtt_subscribe(&client, &sub_list);
}

/* ── MQTT event handler ──────────────────────────────────────────────────── */

static void mqtt_evt_handler(struct mqtt_client *const c,
			     const struct mqtt_evt *evt)
{
	switch (evt->type) {

	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			LOG_INF("MQTT CONNACK – connected to Blynk");
			mqtt_connected = true;
			subscribe_topics();
			publish_device_info();
			if (first_connect) {
				first_connect = false;
				k_sem_give(&cloud_connected_sem);
			}
		} else {
			LOG_ERR("MQTT CONNACK error: %d", evt->result);
		}
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT disconnected (result=%d)", evt->result);
		mqtt_connected = false;
		break;

	case MQTT_EVT_SUBACK:
		LOG_INF("MQTT SUBACK (id=%u)", evt->param.suback.message_id);
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &evt->param.publish;
		size_t     topic_len   = p->message.topic.topic.size;
		const char *topic_utf8 = (const char *)p->message.topic.topic.utf8;
		size_t     pay_len     = p->message.payload.len;

		if (pay_len >= PAYLOAD_BUF_SIZE) {
			pay_len = PAYLOAD_BUF_SIZE - 1;
		}

		int rc = mqtt_read_publish_payload_blocking(c, payload_buf, pay_len);

		if (rc < 0) {
			LOG_ERR("Failed to read MQTT payload: %d", rc);
			break;
		}
		payload_buf[rc] = '\0';

		LOG_INF("PUBLISH ← topic=%.*s", (int)topic_len, topic_utf8);

		if (topic_len == sizeof("downlink/redirect") - 1 &&
		    strncmp(topic_utf8, "downlink/redirect", topic_len) == 0) {
			handle_redirect(payload_buf, (size_t)rc);

		} else if (topic_len == sizeof("downlink/ota/json") - 1 &&
			   strncmp(topic_utf8, "downlink/ota/json", topic_len) == 0) {
			handle_ota(payload_buf, (size_t)rc);

		} else if (topic_len > sizeof("downlink/ds/") - 1 &&
			   strncmp(topic_utf8, "downlink/ds/",
				   sizeof("downlink/ds/") - 1) == 0) {
			handle_ds_downlink(topic_utf8, topic_len,
					   payload_buf, (size_t)rc);
		}

		/* ACK for QoS 1 */
		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param puback = {
				.message_id = p->message_id,
			};

			mqtt_publish_qos1_ack(c, &puback);
		}
		break;
	}

	case MQTT_EVT_PUBACK:
		LOG_DBG("MQTT PUBACK (id=%u)", evt->param.puback.message_id);
		break;

	case MQTT_EVT_PINGRESP:
		LOG_DBG("MQTT PINGRESP");
		break;

	default:
		LOG_DBG("MQTT event type %d", evt->type);
		break;
	}
}

/* ── Broker DNS resolution ───────────────────────────────────────────────── */

static int broker_setup(const char *host)
{
	struct addrinfo hints = {
		.ai_family   = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct addrinfo *res = NULL;

	LOG_INF("Resolving MQTT broker: %s", host);

	int err = getaddrinfo(host, NULL, &hints, &res);

	if (err || !res) {
		LOG_ERR("getaddrinfo() failed: %d (errno=%d)", err, errno);
		return -EADDRNOTAVAIL;
	}

	struct sockaddr_in *addr = (struct sockaddr_in *)&broker_storage;

	memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
	addr->sin_port   = htons(MQTT_BROKER_PORT);
	addr->sin_family = AF_INET;

	char ip[NET_IPV4_ADDR_LEN];

	net_addr_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
	LOG_INF("Broker → %s:%u", ip, MQTT_BROKER_PORT);

	freeaddrinfo(res);
	return 0;
}

/* ── MQTT client initialisation ──────────────────────────────────────────── */

static struct mqtt_utf8 client_id_utf8;
static struct mqtt_utf8 username_utf8;
static struct mqtt_utf8 password_utf8;

static void mqtt_client_setup(void)
{
	const char *token = credentials_get_auth_token();

	mqtt_client_init(&client);

	client_id_utf8.utf8 = (uint8_t *)token;
	client_id_utf8.size = strlen(token);

	username_utf8.utf8 = (uint8_t *)"device";
	username_utf8.size = sizeof("device") - 1;

	password_utf8.utf8 = (uint8_t *)token;
	password_utf8.size = strlen(token);

	client.broker        = &broker_storage;
	client.evt_cb        = mqtt_evt_handler;
	client.client_id     = client_id_utf8;
	client.user_name     = &username_utf8;
	client.password      = &password_utf8;
	client.keepalive     = MQTT_KEEPALIVE_S;
	client.clean_session = 1;
	client.rx_buf        = mqtt_rx_buf;
	client.rx_buf_size   = sizeof(mqtt_rx_buf);
	client.tx_buf        = mqtt_tx_buf;
	client.tx_buf_size   = sizeof(mqtt_tx_buf);

	client.transport.type                     = MQTT_TRANSPORT_SECURE;
	client.transport.tls.config.peer_verify   = TLS_PEER_VERIFY_REQUIRED;
	client.transport.tls.config.cipher_list   = NULL;
	client.transport.tls.config.cipher_count  = 0;
	client.transport.tls.config.sec_tag_list  = tls_sec_tags;
	client.transport.tls.config.sec_tag_count = ARRAY_SIZE(tls_sec_tags);
	client.transport.tls.config.session_cache = TLS_SESSION_CACHE_ENABLED;
	client.transport.tls.config.hostname      = current_host;
}

/* ── Connect to Blynk broker ─────────────────────────────────────────────── */

static int mqtt_connect_to_blynk(void)
{
	int err;

	err = broker_setup(current_host);
	if (err) {
		return err;
	}

	mqtt_client_setup();

	LOG_INF("Connecting to Blynk MQTT %s:%d …", current_host, MQTT_BROKER_PORT);

	err = mqtt_connect(&client);
	if (err) {
		LOG_ERR("mqtt_connect() failed: %d", err);
		return err;
	}

	/* Wait for CONNACK */
	int64_t start = k_uptime_get();

	while (!mqtt_connected) {
		if (k_uptime_get() - start > MQTT_CONNECT_TIMEOUT_MS) {
			LOG_ERR("MQTT connect timeout");
			mqtt_disconnect(&client, NULL);
			return -ETIMEDOUT;
		}

		struct zsock_pollfd fds = {
			.fd     = client.transport.tls.sock,
			.events = ZSOCK_POLLIN,
		};

		if (zsock_poll(&fds, 1, 500) > 0 &&
		    (fds.revents & ZSOCK_POLLIN)) {
			mqtt_input(&client);
		}
	}

	return 0;
}

/* ── Poll thread ─────────────────────────────────────────────────────────── */

static void poll_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	bool first_attempt = true;

	while (true) {
		if (!mqtt_connected) {
			/* Apply any pending redirect before reconnecting */
			if (redirect_pending) {
				strncpy(current_host, redirect_host,
					BROKER_HOST_SIZE - 1);
				current_host[BROKER_HOST_SIZE - 1] = '\0';
				redirect_pending = false;
			}

			if (!first_attempt) {
				LOG_INF("Reconnecting in %ds…", RECONNECT_DELAY_S);
				k_sleep(K_SECONDS(RECONNECT_DELAY_S));
			}
			first_attempt = false;

			k_mutex_lock(&mqtt_mutex, K_FOREVER);
			mqtt_connect_to_blynk();
			k_mutex_unlock(&mqtt_mutex);
			continue;
		}

		struct zsock_pollfd fds = {
			.fd     = client.transport.tls.sock,
			.events = ZSOCK_POLLIN,
		};

		int rc = zsock_poll(&fds, 1, 5000);

		k_mutex_lock(&mqtt_mutex, K_FOREVER);

		if (rc < 0) {
			LOG_ERR("zsock_poll() failed: %d", errno);
			mqtt_connected = false;
			k_mutex_unlock(&mqtt_mutex);
			continue;
		}

		if (rc > 0 && (fds.revents & ZSOCK_POLLIN)) {
			rc = mqtt_input(&client);
			if (rc && rc != -EAGAIN) {
				LOG_ERR("mqtt_input() failed: %d", rc);
				mqtt_connected = false;
				k_mutex_unlock(&mqtt_mutex);
				continue;
			}
		}

		int live_rc = mqtt_live(&client);

		if (live_rc && live_rc != -EAGAIN) {
			LOG_ERR("mqtt_live() failed: %d", live_rc);
			mqtt_connected = false;
		}

		k_mutex_unlock(&mqtt_mutex);
	}
}

/* ── cloud_wait_connected ────────────────────────────────────────────────── */

void cloud_wait_connected(void)
{
	k_sem_take(&cloud_connected_sem, K_FOREVER);
}

/* ── cloud_publish ───────────────────────────────────────────────────────── */

static int counter;

int cloud_publish(struct device_data *data)
{
	if (fota_in_progress) {
		LOG_INF("OTA in progress – skipping publish");
		return 0;
	}

	if (!mqtt_connected) {
		LOG_WRN("Not connected to Blynk, skipping publish");
		return -ENOTCONN;
	}

	counter++;

	char payload[32];
	int pay_len = snprintk(payload, sizeof(payload), "%d", counter);

	static const char topic[] = "ds/V1";

	struct mqtt_publish_param pub = {
		.message = {
			.topic = {
				.topic = { .utf8 = (uint8_t *)topic,
					   .size = sizeof(topic) - 1 },
				.qos   = MQTT_QOS_0_AT_MOST_ONCE,
			},
			.payload = {
				.data = (uint8_t *)payload,
				.len  = (size_t)pay_len,
			},
		},
		.message_id  = 0,
		.dup_flag    = 0,
		.retain_flag = 0,
	};

	LOG_INF("PUBLISH → %s = %s", topic, payload);

	k_mutex_lock(&mqtt_mutex, K_FOREVER);
	int err = mqtt_publish(&client, &pub);
	k_mutex_unlock(&mqtt_mutex);

	if (err) {
		LOG_ERR("mqtt_publish() failed: %d", err);
	}

	return err;
}

/* ── cloud_publish_button ────────────────────────────────────────────────── */

int cloud_publish_button(void)
{
	if (!mqtt_connected) {
		LOG_WRN("Not connected to Blynk, skipping button publish");
		return -ENOTCONN;
	}

	/* Format current time as "YYYY-MM-DD HH:MM:SS" */
	char payload[32] = "unknown";
	int64_t unix_ms;

	if (date_time_now(&unix_ms) == 0) {
		time_t t = (time_t)(unix_ms / 1000);
		struct tm *tm = gmtime(&t);

		snprintk(payload, sizeof(payload), "%04d-%02d-%02d %02d:%02d:%02d",
			 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			 tm->tm_hour, tm->tm_min, tm->tm_sec);
	}

	static const char topic[] = "ds/V2";

	struct mqtt_publish_param pub = {
		.message = {
			.topic = {
				.topic = { .utf8 = (uint8_t *)topic,
					   .size = sizeof(topic) - 1 },
				.qos   = MQTT_QOS_0_AT_MOST_ONCE,
			},
			.payload = {
				.data = (uint8_t *)payload,
				.len  = strlen(payload),
			},
		},
		.message_id  = 0,
		.dup_flag    = 0,
		.retain_flag = 0,
	};

	LOG_INF("PUBLISH → %s = %s (button pressed)", topic, payload);

	k_mutex_lock(&mqtt_mutex, K_FOREVER);
	int err = mqtt_publish(&client, &pub);
	k_mutex_unlock(&mqtt_mutex);

	if (err) {
		LOG_ERR("mqtt_publish() failed: %d", err);
	}
	return err;
}

/* ── Certificate provisioning ────────────────────────────────────────────── */

static int cert_provision(void)
{
	int err;
	bool exists;
	int mismatch;

	err = modem_key_mgmt_exists(CONFIG_CLOUD_TLS_SEC_TAG,
				    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
	if (err) {
		LOG_ERR("Failed to check for certificate: %d", err);
		return err;
	}

	if (exists) {
		mismatch = modem_key_mgmt_cmp(CONFIG_CLOUD_TLS_SEC_TAG,
					      MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
					      cert, strlen(cert));
		if (!mismatch) {
			LOG_INF("Certificate match");
			return 0;
		}

		LOG_INF("Certificate mismatch – re-provisioning");
		err = modem_key_mgmt_delete(CONFIG_CLOUD_TLS_SEC_TAG,
					    MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
		if (err) {
			LOG_ERR("Failed to delete old certificate: %d", err);
		}
	}

	LOG_INF("Provisioning CA certificate to modem");
	err = modem_key_mgmt_write(CONFIG_CLOUD_TLS_SEC_TAG,
				   MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
				   cert, sizeof(cert) - 1);
	if (err) {
		LOG_ERR("Failed to provision certificate: %d", err);
	}
	return err;
}

/* ── cloud_init ──────────────────────────────────────────────────────────── */

int cloud_init(void (*callback)(struct device_data *data))
{
	int err;

	if (!callback) {
		return -EINVAL;
	}

	cloud_callback = callback;

	strncpy(current_host, credentials_get_server(), BROKER_HOST_SIZE - 1);
	current_host[BROKER_HOST_SIZE - 1] = '\0';

	err = cert_provision();
	if (err) {
		LOG_ERR("Certificate provisioning failed: %d", err);
	}

	err = fota_download_init(fota_callback);
	if (err) {
		LOG_ERR("fota_download_init() failed: %d", err);
		return err;
	}

	return 0;
}

/* ── cloud_start ─────────────────────────────────────────────────────────── */

void cloud_start(void)
{
	k_thread_create(&poll_thread, poll_stack,
			K_THREAD_STACK_SIZEOF(poll_stack),
			poll_thread_fn, NULL, NULL, NULL,
			POLL_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&poll_thread, "mqtt_poll");
}
