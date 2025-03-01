#pragma once
#include <mqtt.h>

typedef enum {
    topic_connect,
    topic_disconnect,
    topic_redirect,
    topic_reboot,
    topic_ping,
    topic_power,
    topic_settemperature,
    topic_unknown
} topic_id_t;

#define MQTT_QUEUE_SIZE 2  // Max queued messages
extern QueueHandle_t mqttQueue;

typedef struct {
    topic_id_t id;
    uint8_t* payload;
    uint32_t payload_len;
} mqtt_message_t;

err_t mqtt_init();
err_t mqtt_connect(const char* host);
err_t mqtt_send_connect();
err_t mqtt_publish_ds(const char* ds, const char* payload);

#define DBG(fmt...) printf("DBG: " fmt)
#define INF(fmt...) printf("INF: " fmt)
#define ERR(fmt...) printf("ERR: " fmt)
// #define DBG(fmt, ...)
// #define INF(fmt, ...)
// #define ERR(fmt, ...)