#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <mqtt.h>
#include "cmsis_os.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/altcp_tls.h"
#include "lwip/tcpip.h"
#include "mbedtls/debug.h"
#include "client_blynk.h"

#define MAX_HOST_LEN 255
#define MAX_URL_LEN 512

#define MQTT_CLIENT_ID ""
#define MQTT_CLIENT_USER "device"
#define MQTT_CLIENT_DEV_TOKEN "ggqUax2lkmk-ZYrS8BTTpmWLRTsZ7S28"
#define MQTT_KEEP_ALIVE 45

#ifndef MQTT_CLIENT_DEV_TOKEN
#error "MQTT_CLIENT_DEV_TOKEN is not defined. Please define a valid device token."
#endif

/* Struct to hold publish parameters */
typedef struct {
   char topic[64];
   char message[128];
} mqtt_publish_data_t;

/* Struct to map id to topic name */
typedef struct{
   topic_id_t id;
   const char* topic;
} topic_info;

static topic_info topic_map[] = {
   {topic_redirect, "downlink/redirect"},
   {topic_reboot, "downlink/reboot"},
   {topic_ping, "downlink/ping"},
   {topic_power, "downlink/ds/Power"},
   {topic_settemperature, "downlink/ds/Set Temperature"}  
};

// externals
extern mbedtls_ssl_config conf;  // SSL configuration
extern const char* CA_CERT;

QueueHandle_t mqttQueue;

//redirection handling
static char* redirect_url = NULL;
static topic_id_t current_topic = topic_unknown;
static mqtt_client_t* mqtt_client = NULL;

err_t mqtt_connect(const char* url);

static struct mqtt_connect_client_info_t* prep_mqtt_connect_client_info(bool secure) {
   static struct mqtt_connect_client_info_t mqtt_connect_client_info = {0};

   mqtt_connect_client_info.client_id = MQTT_CLIENT_ID;
   mqtt_connect_client_info.client_user = MQTT_CLIENT_USER;
   mqtt_connect_client_info.client_pass = MQTT_CLIENT_DEV_TOKEN;
   mqtt_connect_client_info.keep_alive = MQTT_KEEP_ALIVE;

   if(secure) {
      if(NULL == mqtt_connect_client_info.tls_config) {
         struct altcp_tls_config* ptls_config = altcp_tls_create_config_client((uint8_t*)CA_CERT, strlen(CA_CERT) + 1);
         mqtt_connect_client_info.tls_config = ptls_config;
      }
   }

   return &mqtt_connect_client_info;
}

static void check_DNS() {
   if (IP_ADDR_ANY == dns_getserver(0)) {
      ip_addr_t addr;
      INF("DNS server not set, setting default DNS to 8.8.8.8\n");
      inet_pton(AF_INET, "8.8.8.8", &addr);
      dns_setserver(0, &addr);
   }
}

static err_t resolve_host_IP(const char* host, ip_addr_t* addr) {
   DBG("Trying to resolve: %s\n", host);
   err_t err;
   while (err = dns_gethostbyname(host, addr, NULL, NULL), err == ERR_INPROGRESS) {
      osDelay(1000);
   }
   if (ERR_OK == err)
      DBG("%s\n", ip4addr_ntoa(addr));
   else
      ERR("resolve_host_IP failed!\n");
   return err;
}

// rough check for memory allocation
static __attribute__((unused)) void check_malloc_caps() {
   size_t kb = 0, alloc_size;
   void* ptr;
   while ((alloc_size = (kb++ + 1) * 1024), (ptr = malloc(alloc_size))) {
      DBG("Allocated %u KB\n", kb);
      free(ptr);
   }
   INF("\nMax allocation allowed: %u KB\n", kb * 1024);
}

static void mqtt_incoming_publish_cb(void* arg, const char* topic, u32_t tot_len);
static void mqtt_incoming_data_cb(void* arg, const u8_t* data, u16_t len, u8_t flags);
static void mqtt_connection_cb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status);
static void mqtt_pub_request_cb(void* arg, err_t result);

/* Safe publish using tcpip_callback */
static void mqtt_publish_callback(void* arg) {
   mqtt_publish_data_t* data = (mqtt_publish_data_t*)arg;
   if(mqtt_client == NULL) {
      goto fail;
   }

   err_t err = mqtt_publish(mqtt_client, data->topic, data->message, strlen(data->message), 1, 0, mqtt_pub_request_cb, NULL);
   if(err != ERR_OK) {
      ERR("%s(%s, %s) error: %d\n", __PRETTY_FUNCTION__, data->topic, data->message, err);
   }

fail:
   if(data)
      free(data);
}

static bool parse_mqtt_url(const char *url, size_t url_len, bool *is_secure, char **host, uint16_t *port) {
    if (!url || url_len == 0 || !is_secure || !host || !port) {
        return false;  // Null check
    }

    // Allocate memory for a null-terminated copy of the URL
    size_t copy_len = (url_len < MAX_URL_LEN) ? url_len : MAX_URL_LEN;
    char *temp_url = (char *)malloc(copy_len + 1);
    if (!temp_url) return false;  // Memory allocation failure

    memcpy(temp_url, url, copy_len);
    temp_url[copy_len] = '\0';  // Ensure null termination

    // Determine if it's secure
    if (strncmp(temp_url, "mqtts://", 8) == 0) {
        *is_secure = true;
        url = temp_url + 8;  // Move past "mqtts://"
    } else if (strncmp(temp_url, "mqtt://", 7) == 0) {
        *is_secure = false;
        url = temp_url + 7;  // Move past "mqtt://"
    } else {
        free(temp_url);
        return false; // Invalid protocol
    }

    // Allocate memory for host storage
    char *temp_host = (char *)malloc(MAX_HOST_LEN + 1);
    if (!temp_host) {
        free(temp_url);
        return false;  // Memory allocation failure
    }

    // Parse host and port safely
    int scanned = sscanf(url, "%255[^:]:%hu", temp_host, port); // Limit host length
    if (scanned == 2) {  // Successfully parsed host and port
        *host = temp_host;  // Assign the allocated host buffer to the output
        free(temp_url);  // Free temp URL copy
        return true;
    }

    // Cleanup on failure
    free(temp_host);
    free(temp_url);
    return false;
}

static void mqtt_incoming_publish_cb(void* arg, const char* topic, u32_t tot_len) {
   DBG("%s(%p, %s, %lu)\n", __PRETTY_FUNCTION__, arg, topic, tot_len);
   current_topic = topic_unknown;
   for(size_t index = 0; index < sizeof(topic_map) / sizeof(topic_map[0]); index++) {
      if(0 == strcmp(topic_map[index].topic, topic)) {
         current_topic = topic_map[index].id;
      }
   }
}

static void mqtt_incoming_data_cb(void* arg, const u8_t* data, u16_t len, u8_t flags)
{
   DBG("%s(%p, %p, %u, %u)\n", __PRETTY_FUNCTION__, arg, data, len, flags);
   if (flags & MQTT_DATA_FLAG_LAST) {
      /* Last fragment of payload received (or whole part if payload fits receive buffer
         See MQTT_VAR_HEADER_BUFFER_LEN)  */
      switch(current_topic) {
      case topic_redirect:
         if( data && len ){
            if( redirect_url ) free(redirect_url);
            redirect_url = malloc(len+sizeof(char));
            if(redirect_url){
               strncpy(redirect_url, (char*)data, len);
               redirect_url[len] = '\0';
            }
         }
         break;
      default:
         if(current_topic != topic_unknown){
            mqtt_message_t msg = {0};
            msg.id = current_topic;
            if(data && len) {
               msg.payload = malloc(len);
               if(msg.payload) {
                  memcpy(msg.payload, data, len);
                  msg.payload_len = len;
               }else{
                  ERR("%s() : can't alloc memory!\n", __PRETTY_FUNCTION__);
               }
            }
            xQueueSend(mqttQueue, &msg, portMAX_DELAY);
         }
         break;
      }
   }
   else {
      /* Handle fragmented payload, store in buffer, write to file or whatever */
   }
}

static void mqtt_sub_request_cb(void* arg, err_t result)
{
   DBG("%s(%p, %d)\n", __PRETTY_FUNCTION__, arg, result);
}

static void mqtt_pub_request_cb(void* arg, err_t result)
{
   DBG("%s(%p, %d)\n", __PRETTY_FUNCTION__, arg, result);
}

static void mqtt_connection_cb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status)
{
   INF("%s(%p, %p, %d)\n", __PRETTY_FUNCTION__, client, arg, status);
   err_t err;
   if (status == MQTT_CONNECT_ACCEPTED) {
      /* Setup callback for incoming publish requests */
      mqtt_set_inpub_callback(client, mqtt_incoming_publish_cb, mqtt_incoming_data_cb, arg);
      /* Subscribe to a topic named "subtopic" with QoS level 1, call mqtt_sub_request_cb with result */
      err = mqtt_subscribe(client, "downlink/#", 1, mqtt_sub_request_cb, arg);
      if (err != ERR_OK) {
         ERR("%s() error: %d\n", __PRETTY_FUNCTION__, err);
      }
   }
   else {
      mqtt_message_t msg = {0};
      if(current_topic == topic_redirect && redirect_url) {
         INF("Redirection to %s\n", redirect_url);
         msg.id = topic_redirect;
         msg.payload = (uint8_t*)redirect_url;
         msg.payload_len = strlen(redirect_url) + sizeof(char);
         xQueueSend(mqttQueue, &msg, portMAX_DELAY);
         redirect_url = NULL;
      }
      else {
         msg.id = topic_disconnect;
         xQueueSend(mqttQueue, &msg, portMAX_DELAY);
      }
   }
}

err_t mqtt_init(){
    mqttQueue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(mqtt_message_t));
    return mqttQueue ? ERR_OK:ERR_MEM;
}

err_t mqtt_connect(const char* url) {
   err_t err;
   ip_addr_t addr;
   char* host = NULL;
   bool is_secure;
   uint16_t port;

   mbedtls_debug_set_threshold(0);

   if(mqtt_client) {
      mqtt_disconnect(mqtt_client);
      mqtt_client_free(mqtt_client); //free old client
   }

   mqtt_client = mqtt_client_new();
   if(NULL == mqtt_client) {
      ERR("mqtt_client_new() returned NULL");
      err = ERR_MEM;
      goto fail;
   }

   if(!parse_mqtt_url(url, strlen(url), &is_secure, &host, &port)) {
      ERR("mqtt_client_new() fail parsing input URL");
      err = ERR_ARG;
      goto fail;
   }

   check_DNS();
   err = resolve_host_IP(host, &addr);
   if(ERR_OK != err) {
      ERR("DNS resolve fail: %d\n", err);
      goto fail;
   }

   err = mqtt_client_connect(mqtt_client,
                             &addr,
                             port,
                             mqtt_connection_cb,
                             0,
                             prep_mqtt_connect_client_info(is_secure));
   if(err != ERR_OK) {
      ERR("mqtt_client_connect(...) fail: %d\n", err);
      goto fail;
   }

   return err;

fail:
   if(mqtt_client){
      mqtt_client_free(mqtt_client);
      mqtt_client = NULL;
   }
   if(host)
      free(host);
   return err;
}

err_t mqtt_send_connect()
{
   mqtt_message_t msg = {0};
   msg.id = topic_connect;
   xQueueSend(mqttQueue, &msg, portMAX_DELAY);
   return ERR_OK;
}

err_t mqtt_publish_ds(const char* ds, const char* payload) {
   if(mqtt_client == NULL)
      return ERR_CONN;

   mqtt_publish_data_t* data = malloc(sizeof(mqtt_publish_data_t));
   if(data == NULL) {
      ERR("%s(), mem allocation failed!\n", __PRETTY_FUNCTION__);
      return ERR_MEM;
   }

   strncpy(data->topic, ds, sizeof(data->topic));
   strncpy(data->message, payload, sizeof(data->message));
   data->topic[sizeof(data->topic) - 1] = '\0';
   data->message[sizeof(data->message) - 1] = '\0';

   return tcpip_callback(mqtt_publish_callback, data);
}
