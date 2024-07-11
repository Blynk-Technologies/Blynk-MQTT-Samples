/*
 * SPDX-FileCopyrightText: 2024 Andrii Yarmolenko for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

#include "nvs.h"
#include "nvs_flash.h"

#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "netif/ppp/pppos.h"
#include "netif/ppp/ppp.h"
#include "lwip/err.h"
#include "netif/ppp/pppapi.h"

#include <esp_wifi.h>
#include <cJSON.h>

#include "mqtt_client.h"

#if !defined(CONFIG_BLYNK_FIRMWARE_TYPE) && defined(CONFIG_BLYNK_TEMPLATE_ID)
    #define CONFIG_BLYNK_FIRMWARE_TYPE         CONFIG_BLYNK_TEMPLATE_ID
#endif

#if !defined(CONFIG_BLYNK_FIRMWARE_VERSION)
    #define CONFIG_BLYNK_FIRMWARE_VERSION      "0.0.0"
#endif

#if !defined(CONFIG_BLYNK_TEMPLATE_ID) || !defined(CONFIG_BLYNK_TEMPLATE_NAME)
    #error "Please specify your BLYNK_TEMPLATE_ID and BLYNK_TEMPLATE_NAME"
#endif

#define BLYNK_PARAM_KV(k, v)    k "\0" v "\0"

#if CONFIG_BLYNK_MQTTS_MODE
    #include "root_certificates.h"
    static const char *MQTTTAG = "[MQTTS]";
#else
    static const char *MQTTTAG = "[MQTT]";
#endif

static TickType_t mqtt_offline_timestamp = 0;
static TaskHandle_t modem_task = NULL;
static ppp_pcb *ppp = NULL;
static struct netif ppp_netif;

static void gsm_init(void);

static int get_nvs_blynk_auth_token(char *token, size_t *len)
{
    nvs_handle_t my_handle;

    if (nvs_open("storage", NVS_READONLY, &my_handle) != ESP_OK)
    {
        ESP_LOGE("NVS", "can't open storage");
        return -1;
    }

    const int rc = nvs_get_str(my_handle, "blynk_token", token, len);
    nvs_close(my_handle);

    return rc;
}

static int set_nvs_blynk_auth_token(const char *token)
{
    nvs_handle_t my_handle;

    if (nvs_open("storage", NVS_READWRITE, &my_handle) != ESP_OK)
    {
        ESP_LOGE("NVS", "can't open storage");
        return -1;
    }

    const int rc = (ESP_OK != nvs_set_str(my_handle, "blynk_token", token) || ESP_OK != nvs_commit(my_handle));
    nvs_close(my_handle);

    return rc;
}

static int starts_with(const char *str, const char *prefix)
{
    size_t len_prefix = strlen(prefix);
    size_t len_str = strlen(str);
    if (len_str < len_prefix) {
        return 0;
    }
    return strncmp(str, prefix, len_prefix) == 0;
}

static u32_t p_output_callback(ppp_pcb *pcb, u8_t *data, u32_t len, void *ctx)
{
    const uint32_t ret = uart_write_bytes(CONFIG_GSM_UART_NUM, (const char*)data, len);
    //uart_wait_tx_done(CONFIG_GSM_UART_NUM, 10 / portTICK_PERIOD_MS);
    return ret;
}

static int send_cmd(const uart_port_t uart_num, const char *cmd, const char *rsp1, const char *rsp2, const int timeout_ms)
{
    uart_flush(CONFIG_GSM_UART_NUM);
    uart_write_bytes(CONFIG_GSM_UART_NUM, cmd, strlen(cmd));
    uart_wait_tx_done(CONFIG_GSM_UART_NUM, 10 / portTICK_PERIOD_MS);
#if CONFIG_GSM_AT_CMD_DEBUG
    ESP_LOGI("[GSM]", "snd[%s]", cmd);
#endif
    uint8_t d[256];
    int len = 0;
    for (int i = 0; i < timeout_ms; i += 20)
    {
        const int len_tmp = uart_read_bytes(CONFIG_GSM_UART_NUM, d + len, sizeof(d) - len - 1, 20 / portTICK_PERIOD_MS);
        if (len_tmp)
        {
            len += len_tmp;
            d[len] = '\0';
#if CONFIG_GSM_AT_CMD_DEBUG
            ESP_LOGI("[GSM]", "rcv[%s]", d);
#endif
            if (rsp1 && strstr((const char *)d, rsp1))
            {
                return 1;
            }
            if (rsp2 && strstr((const char *)d, rsp2))
            {
                return 2;
            }
        }
    }
    return -1;
}

static void p_status_cb(ppp_pcb *pcb, int err_code, void *ctx)
{
    LWIP_UNUSED_ARG(ctx);

    struct netif *pppif = ppp_netif(pcb);

    switch (err_code) {
        case PPPERR_NONE:
            ESP_LOGI("[GSM]","status_cb: Connected");
            #if PPP_IPV4_SUPPORT
            ESP_LOGI("[GSM]","   ipaddr    = %s", ipaddr_ntoa(&pppif->ip_addr));
            ESP_LOGI("[GSM]","   gateway   = %s", ipaddr_ntoa(&pppif->gw));
            ESP_LOGI("[GSM]","   netmask   = %s", ipaddr_ntoa(&pppif->netmask));
            #endif

            #if PPP_IPV6_SUPPORT
            ESP_LOGI("[GSM]","   ip6addr   = %s", ip6addr_ntoa(netif_ip6_addr(pppif, 0)));
            #endif
            break;
        case PPPERR_PARAM:
            ESP_LOGE("[GSM]","status_cb: Invalid parameter");
            break;
        case PPPERR_OPEN:
            ESP_LOGE("[GSM]","status_cb: Unable to open PPP session");
            break;
        case PPPERR_DEVICE:
            ESP_LOGE("[GSM]","status_cb: Invalid I/O device for PPP");
            break;
        case PPPERR_ALLOC:
            ESP_LOGE("[GSM]","status_cb: Unable to allocate resources");
            break;
        case PPPERR_USER:
            /* ppp_free(); -- can be called here */
            ESP_LOGW("[GSM]","status_cb: User interrupt (disconnected)");
            break;
        case PPPERR_CONNECT:
            ESP_LOGE("[GSM]","status_cb: Connection lost");
            break;
        case PPPERR_AUTHFAIL:
            ESP_LOGE("[GSM]","status_cb: Failed authentication challenge");
            break;
        case PPPERR_PROTOCOL:
            ESP_LOGE("[GSM]","status_cb: Failed to meet protocol");
            break;
        case PPPERR_PEERDEAD:
            ESP_LOGE("[GSM]","status_cb: Connection timeout");
            break;
        case PPPERR_IDLETIMEOUT:
            ESP_LOGE("[GSM]","status_cb: Idle Timeout");
            break;
        case PPPERR_CONNECTTIME:
            ESP_LOGE("[GSM]","status_cb: Max connect time reached");
            break;
        case PPPERR_LOOPBACK:
            ESP_LOGE("[GSM]","status_cb: Loopback detected");
            break;
        default:
            ESP_LOGE("[GSM]","status_cb: Unknown error code %d", err_code);
            break;
    }

    if (err_code != PPPERR_USER && err_code != PPPERR_NONE && ppp)
    {
       //gsm_init();
    }
}

static void gsm_task(void *pvParameters)
{
    if (uart_is_driver_installed(CONFIG_GSM_UART_NUM))
    {
        uart_driver_delete(CONFIG_GSM_UART_NUM);
    }

    gpio_set_direction(CONFIG_GSM_UART_GPIO_TX, GPIO_MODE_OUTPUT);
    gpio_set_direction(CONFIG_GSM_UART_GPIO_RX, GPIO_MODE_INPUT);
    gpio_set_pull_mode(CONFIG_GSM_UART_GPIO_RX, GPIO_PULLUP_ONLY);

    const uart_config_t uart_config =
    {
        .baud_rate = CONFIG_GSM_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(CONFIG_GSM_UART_NUM, &uart_config);
    uart_set_pin(CONFIG_GSM_UART_NUM, CONFIG_GSM_UART_GPIO_TX, CONFIG_GSM_UART_GPIO_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(CONFIG_GSM_UART_NUM, CONFIG_GSM_BUF_SIZE, CONFIG_GSM_BUF_SIZE, 0, NULL, 0);

    while (1)
    {
        ESP_LOGI("[GSM]","modem init start");

        gpio_set_level(CONFIG_GSM_PWR_ON, 1);
        vTaskDelay(10 / portTICK_PERIOD_MS);
#if (CONFIG_GSM_PWR_KEY > 0)
        gpio_set_level(CONFIG_GSM_PWR_KEY, 0);
        vTaskDelay(1500 / portTICK_PERIOD_MS);
        gpio_set_level(CONFIG_GSM_PWR_KEY, 1);
        vTaskDelay(1500 / portTICK_PERIOD_MS);
#endif
        int i;
        for (i = 0; i < 60; i++)
        {
            const int rc = send_cmd(CONFIG_GSM_UART_NUM, "AT\r\n", "OK", "0", 1000);
            if (1 == rc || 2 == rc)
            {
                break;
            }

        }

        if (i == 60)
        {
            ESP_LOGE("[GSM]","modem not answer");
            continue;
        }
        else
        {
            ESP_LOGI("[GSM]","modem found");
        }
#if CONFIG_GSM_AT_CMD_DEBUG
        send_cmd(CONFIG_GSM_UART_NUM, "AT+CNUM\r\n", "OK", "0", 500);
#endif
        for (i = 0; i < 60; i++)
        {
            const int r = send_cmd(CONFIG_GSM_UART_NUM, "AT+CREG?\r\n", "+CREG: 0,1", "+CREG: 0,5", 1000);
            if (r == 1 || r == 2)
            {
                break;
            }
        }

        if (i == 60)
        {
            ESP_LOGE("[GSM]","modem not registering");
            continue;
        }
        else
        {
            ESP_LOGI("[GSM]","modem registration OK");
        }

        vTaskDelay(2500 / portTICK_PERIOD_MS);
        send_cmd(CONFIG_GSM_UART_NUM, "AT+CGDCONT=1,\"IP\",\"internet\"\r\n", "OK", NULL, 500);
        vTaskDelay(4000 / portTICK_PERIOD_MS);

        if (1 == send_cmd(CONFIG_GSM_UART_NUM, "ATD*99#\r\n", "CONNECT", "ERROR", 15000))
        {
            ESP_LOGI("[GSM]","modem connected");

            memset(&ppp_netif, 0, sizeof(ppp_netif));
            ppp = pppapi_pppos_create(&ppp_netif, p_output_callback, p_status_cb, NULL);
            pppapi_set_default(ppp);
            pppapi_connect(ppp, 0);

            uint8_t d1[CONFIG_GSM_BUF_SIZE];
            while (1)
            {
                const int len = uart_read_bytes(CONFIG_GSM_UART_NUM, d1, CONFIG_GSM_BUF_SIZE, 10 / portTICK_PERIOD_MS);
                if (len)
                {
                    pppos_input_tcpip(ppp, (u8_t*)d1, len);
                }
            }
        }
        else
        {
            ESP_LOGE("[GSM]","modem can't connect");
        }
    }
}

static void gsm_init(void)
{
    if (ppp)
    {
        ESP_LOGI("[GSM]","disable PPP interface");
        pppapi_close(ppp, 0);
        pppapi_free(ppp);
        ppp = NULL;
    }

    if (modem_task)
    {
        ESP_LOGI("[GSM]","restart modem task");
        vTaskDelete(modem_task);
        modem_task = NULL;
    }

    xTaskCreate(gsm_task,
                "gsm_task",
                4096 * 2,
                NULL,
                4,
                &modem_task);
}

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(MQTTTAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(MQTTTAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    char s[64];
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_CONNECTED");
            mqtt_offline_timestamp = 0;

            esp_mqtt_client_subscribe(client, "downlink/#", 0);

            cJSON *root;
            root = cJSON_CreateObject();
            cJSON_AddItemToObject(root, "tmpl",  cJSON_CreateString(CONFIG_BLYNK_TEMPLATE_ID));
            cJSON_AddItemToObject(root, "ver",   cJSON_CreateString(CONFIG_BLYNK_FIRMWARE_VERSION));
            cJSON_AddItemToObject(root, "build", cJSON_CreateString(__DATE__ " " __TIME__));
            cJSON_AddItemToObject(root, "type",  cJSON_CreateString(CONFIG_BLYNK_FIRMWARE_TYPE));
            cJSON_AddNumberToObject(root, "rxbuff", 512);
            char *rendered=cJSON_Print(root);
            esp_mqtt_client_publish(client, "info/mcu", rendered, 0, 1, 0);
            free(rendered);
            cJSON_Delete(root);

            snprintf(s, sizeof(s), "%d", rand() % 100);
            esp_mqtt_client_publish(client, "ds/Random", s, 0, 1, 0);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_DISCONNECTED");
            if (!mqtt_offline_timestamp)
            {
                ESP_LOGI(MQTTTAG, "just got offline");
                mqtt_offline_timestamp = xTaskGetTickCount();
            }
            else
            {
                const TickType_t now = xTaskGetTickCount();
                const int offline_seconds = (now - mqtt_offline_timestamp) * portTICK_PERIOD_MS / 1000;
                const int gsm_reinit = offline_seconds >= 180;

                ESP_LOGI(MQTTTAG, "offline already %d seconds %s", offline_seconds, gsm_reinit ? "rebooting gsm" : "");

                if (gsm_reinit)
                {
                    mqtt_offline_timestamp = now;
                    gsm_init();
                }
            }
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_DATA");

            const char ota_topic_name[] = "downlink/ota/json";
            const size_t ota_topic_len = strlen(ota_topic_name);

            const char redir_topic_name[] = "downlink/redirect";
            const size_t redir_topic_len = strlen(redir_topic_name);

            if ((event->topic_len >= ota_topic_len)
                && !memcmp(event->topic, ota_topic_name, ota_topic_len))
            {
                ESP_LOGI("[OTA]", "start");
                cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
                ESP_LOGI("[OTA]", "url[%s]", cJSON_GetObjectItem(root, "url")->valuestring);
                ESP_LOGI("[OTA]", "size[%d]", cJSON_GetObjectItem(root, "size")->valueint);
                ESP_LOGI("[OTA]", "type[%s]", cJSON_GetObjectItem(root, "type")->valuestring);
                ESP_LOGI("[OTA]", "ver[%s]", cJSON_GetObjectItem(root, "ver")->valuestring);
                ESP_LOGI("[OTA]", "build[%s]", cJSON_GetObjectItem(root, "build")->valuestring);

                esp_http_client_config_t config = {
                    .url = cJSON_GetObjectItem(root, "url")->valuestring,
                    .cert_pem = (char *)CA_CERT,
                    .keep_alive_enable = true,
                };

                esp_https_ota_config_t ota_config = {
                    .http_config = &config,
                };
                ESP_LOGI("[OTA]", "Attempting to download update from %s", config.url);
                esp_err_t ret = esp_https_ota(&ota_config);
                if (ret == ESP_OK) {
                    ESP_LOGI("[OTA]", "OTA Succeed, Rebooting...");
                    esp_restart();
                } else {
                    ESP_LOGE("[OTA]", "Firmware upgrade failed [%d]", ret);
                }
                cJSON_Delete(root);
            }
            else if ((event->topic_len >= redir_topic_len)
                    && !memcmp(event->topic, redir_topic_name, redir_topic_len))
            {
                ESP_LOGI(MQTTTAG, "got redirect to [%.*s]", event->data_len, event->data);

                char *uri = malloc(event->data_len + 128);
                if (uri)
                {
                    memcpy(uri, event->data, event->data_len);
                    uri[event->data_len] = '\0';

                    char prot[32];
                    char addr[128];
                    int port;

                    while (strchr(uri, '/'))
                    {
                        *(char*)strchr(uri, '/') = ' ';
                    }

                    while (strchr(uri, ':'))
                    {
                        *(char*)strchr(uri, ':') = ' ';
                    }

                    if (3 == sscanf(uri, "%s %s %d", prot, addr, &port))
                    {
                        uri[0] = '\0';
#if CONFIG_BLYNK_MQTTS_MODE
                        if (!strcmp(prot, "tls") || !strcmp(prot, "mqtts"))
                        {
                            strcat(uri, "mqtts://");
                        }
#endif
                        if (!strcmp(prot, "tcp") || !strcmp(prot, "mqtt"))
                        {
                            strcat(uri, "mqtt://");
                        }
                        if (!strcmp(prot, "ws") || !strcmp(prot, "wss"))
                        {
                            sprintf(uri, "%s://", prot);
                        }
                        if (!strstr(uri, "://"))
                        {
                            ESP_LOGE(MQTTTAG, "wrong protocol [%s]", prot);
                        }
                        else
                        {
                            char t[34];
                            size_t l = sizeof(t);
                            if (ESP_OK != get_nvs_blynk_auth_token(t, &l))
                            {
                                ESP_LOGE(MQTTTAG, "Can't read auth token. please configure it and reboot");
                            }

                            sprintf(uri + strlen(uri), "device:%s@%s:%u", t, addr, port);
                            const esp_err_t r0 = esp_mqtt_client_disconnect(client);
                            const esp_err_t r1 = esp_mqtt_client_set_uri(client, uri);
                            if (ESP_OK != r1 || ESP_OK != r0)
                            {
                                ESP_LOGE(MQTTTAG, "can't redirect to [%s] [%s] [%s]", uri,  esp_err_to_name(r0), esp_err_to_name(r1));
                            }
                        }
                    }
                    else
                    {
                        ESP_LOGE(MQTTTAG, "malformed redirect address [%.*s]", event->data_len, event->data);
                    }
                    free(uri);
                }
                else
                {
                    ESP_LOGE(MQTTTAG, "can't allocate uri");
                }
            }
            ESP_LOGI(MQTTTAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGI(MQTTTAG, "DATA=%.*s", event->data_len, event->data);

            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(MQTTTAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(MQTTTAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

            }
            break;
        default:
            ESP_LOGI(MQTTTAG, "Other event id:%d", event->event_id);
            break;
    }
}

static esp_mqtt_client_handle_t* mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    char uri[256];

    if (!starts_with(CONFIG_BLYNK_TEMPLATE_ID, "TMPL") ||
        !strlen(CONFIG_BLYNK_TEMPLATE_NAME)
    ) {
        ESP_LOGE(MQTTTAG, "Invalid configuration of TEMPLATE_ID / TEMPLATE_NAME");
        return NULL;
    }

    char t[34];
    size_t l = sizeof(t);
    if (ESP_OK != get_nvs_blynk_auth_token(t, &l))
    {
        ESP_LOGE(MQTTTAG, "Can't read auth token. please configure it and reboot");
        return NULL;
    }

#if CONFIG_BLYNK_MQTTS_MODE
    snprintf(uri, sizeof(uri), "mqtts://device:%s@%s:8883", t, CONFIG_BLYNK_SERVER);
    mqtt_cfg.broker.verification.certificate = (const char *)CA_CERT;
#else
    snprintf(uri, sizeof(uri), "mqtt://device:%s@%s:1883", t, CONFIG_BLYNK_SERVER);
#endif

    uri[sizeof(uri) - 1] = '\0';
    mqtt_cfg.broker.address.uri = uri;
    // mqtt_cfg.network.disable_auto_reconnect = true;

    esp_mqtt_client_handle_t *client = malloc(sizeof(esp_mqtt_client_handle_t));

    if (client)
    {
        *client = esp_mqtt_client_init(&mqtt_cfg);

        if (ESP_OK != esp_mqtt_client_register_event(*client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL) ||
            ESP_OK != esp_mqtt_client_start(*client))
        {
            free(client);
            client = NULL;
        }
    }

    return client;
}

static void brd_init(void)
{
    const gpio_config_t io_conf =
    {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
#if (CONFIG_GSM_PWR_KEY > 0)
        .pin_bit_mask = ((1ULL<<CONFIG_GSM_PWR_KEY) | (1ULL<<CONFIG_GSM_PWR_ON)),
#else
        .pin_bit_mask = ((1ULL<<CONFIG_GSM_PWR_ON)),
#endif
        .pull_down_en = 0,
        .pull_up_en = 0,
    };

    gpio_config(&io_conf);
}

int nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return ESP_OK != err;
}

static int blynk_cmd_cb(int argc, char **argv)
{
    if (argc == 2)
    {
        printf("%s\n", set_nvs_blynk_auth_token(argv[1]) ? "error" : "ok");
    }
    else
    {
        printf("wrong usage\n");
    }
    return 0;
}

typedef struct {
    struct arg_str *token;
    struct arg_end *end;
} auth_cmd_t;

static esp_console_cmd_t auth_cmd = {};
static auth_cmd_t auth_cmd_args = {};

static void register_auth_key_cmd(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "BLYNK>";
    repl_config.max_cmdline_length = 256;

    auth_cmd_args.token = arg_str0(NULL, NULL, "<token>", "blynk auth token");
    auth_cmd_args.end = arg_end(1);

    auth_cmd.command = "set_auth_token";
    auth_cmd.help = "blynk token configuration";
    auth_cmd.hint = NULL;
    auth_cmd.func = &blynk_cmd_cb;
    auth_cmd.argtable = &auth_cmd_args;
    esp_console_cmd_register(&auth_cmd);

    const esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_console_new_repl_uart(&hw_config, &repl_config, &repl);

    esp_console_start_repl(repl);
}

//=============
void app_main()
{
    /*
     * Embed the info tag into the MCU firmware binary
     * This structure is used to identify the firmware type
     * and version during the OTA upgrade
     */
    volatile const char firmwareTag[] = "blnkinf\0"
    BLYNK_PARAM_KV("mcu"    , CONFIG_BLYNK_FIRMWARE_VERSION)       // Primary MCU: firmware version
    BLYNK_PARAM_KV("fw-type", CONFIG_BLYNK_FIRMWARE_TYPE)          // Firmware type (usually same as Template ID)
    BLYNK_PARAM_KV("build"  , __DATE__ " " __TIME__)               // Firmware build date and time
    "\0";
    (void)firmwareTag;

    brd_init();

    nvs_init();

    register_auth_key_cmd();

    esp_netif_init();

    gsm_init();

    mqtt_app_start();

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
