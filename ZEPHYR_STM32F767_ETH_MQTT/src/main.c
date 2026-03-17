/*
 * SPDX-FileCopyrightText: 2025 Khrystyna Olkhovetska for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(mqtt_blynk, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/socket.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/random/random.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

#include "root_certificates.h"
#include "mqtt_client.h"
#include "network_manager.h"
#include "ota_manager.h"
#include "utils.h"

BUILD_ASSERT(1 != sizeof(CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION), "CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION is required");
BUILD_ASSERT(1 != sizeof(CONFIG_CLOUD_BLYNK_TEMPLATE_NAME), "CONFIG_CLOUD_BLYNK_TEMPLATE_NAME is required");
BUILD_ASSERT(1 != sizeof(CONFIG_CLOUD_BLYNK_AUTH_TOKEN), "CONFIG_CLOUD_BLYNK_AUTH_TOKEN is required");
BUILD_ASSERT(1 != sizeof(CONFIG_CLOUD_BLYNK_TEMPLATE_ID), "CONFIG_CLOUD_BLYNK_TEMPLATE_ID is required");

bool power_on       = false;
float target_temp   = 23.0f;
float current_temp  = 15.0f;

/* Work items for periodic tasks */
static struct k_work_delayable device_update_work;

/* Function to get random publish interval */
static int get_random_publish_interval(void)
{
    return 10 + (sys_rand8_get() % 5); // 10-15 seconds
}

/* Publish firmware and device info message for Blynk OTA */
static void publish_firmware_info(void)
{
    char info_payload[512];

    /* Construct firmware info JSON matching Blynk specification */
    snprintf(info_payload, sizeof(info_payload),
        "{"
            "\"tmpl\":\"%s\","
            "\"ver\":\"%s\","
            "\"fw-type\":\"%s\","
            "\"build\":\"%s\","
            "\"blynk\":\"1.3.0\","
            "\"board\":\"%s\","
            "\"conn\":\"WiFi\","
            "\"rxbuff\":%d"
        "}",
        CONFIG_CLOUD_BLYNK_TEMPLATE_NAME,
        CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION,
        CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE,
        __DATE__ " " __TIME__,
        CONFIG_BOARD,
        APP_MQTT_BUFFER_SIZE
    );

    /* Publish to info/mcu topic - this is critical for Blynk OTA */
    publish_str("info/mcu", info_payload);

    LOG_INF("Published firmware info: type=%s, ver=%s",
            CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE, CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION);
}

/* Device simulation and business logic */
static void update_device_simulation(void)
{
    float target = power_on ? target_temp : 10.0f;

    /* Simulate temperature change */
    current_temp += (target - current_temp) * 0.05f;
    current_temp += ((0.5f - ((float)(sys_rand32_get() % 1000) / 1000.0f)) * 0.3f);
    current_temp = fminf(fmaxf(current_temp, 10.0f), 35.0f);

    /* Publish temperature */
    char temp_str[16];
    snprintf(temp_str, sizeof(temp_str), "%.2f", (double)current_temp);
    publish_str("ds/Current Temperature", temp_str);

    /* Calculate and publish state */
    int state = 1;  /* Off */
    if (power_on) {
        if (fabsf(current_temp - target_temp) < 1.0f) {
            state = 2;  /* At temperature */
        } else if (target_temp > current_temp) {
            state = 3;  /* Heating */
        } else {
            state = 4;  /* Cooling */
        }
    }

    /* State colors */
    const char *colors[] = { NULL, "E4F6F7", "E6F7E4", "F7EAE4", "E4EDF7" };

    char state_str[8];
    snprintf(state_str, sizeof(state_str), "%d", state);
    publish_str("ds/Status", state_str);

    if (colors[state]) {
        publish_str("ds/Status/prop/color", colors[state]);
    }
}

static void device_update_work_handler(struct k_work *work)
{
    update_device_simulation();
    k_work_reschedule(&device_update_work, K_SECONDS(get_random_publish_interval()));
}

static void handle_terminal_command(const char *payload)
{
    char cmd_buf[64];
    strncpy(cmd_buf, payload, sizeof(cmd_buf) - 1);
    cmd_buf[sizeof(cmd_buf) - 1] = '\0';

    char *cmd = strtok(cmd_buf, " \r\n");
    if (!cmd) {
        return;
    }

    if (strcmp(cmd, "set") == 0) {
        char *arg = strtok(NULL, " \r\n");
        if (arg) {
            target_temp = atof(arg);
            terminal_print("Temperature set");

            char val[16];
            snprintf(val, sizeof(val), "%.1f", (double)target_temp);
            publish_str("ds/Set Temperature", val);
        }
    } else if (strcmp(cmd, "on") == 0) {
        power_on = true;
        publish_str("ds/Power", "1");
        terminal_print("Turned ON");
    } else if (strcmp(cmd, "off") == 0) {
        power_on = false;
        publish_str("ds/Power", "0");
        terminal_print("Turned OFF");
    } else if (strcmp(cmd, "info") == 0) {
        /* Republish firmware info for testing */
        publish_firmware_info();
        terminal_print("Firmware info published");
    } else if (strcmp(cmd, "ota") == 0) {
        /* Test OTA with example payload */
        const char *test_ota =
            "{"
            "\"url\":\"https://blynk.cloud/static/test.bin?token=example\","
            "\"size\":32768,"
            "\"type\":\"" CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE "\","
            "\"ver\":\"1.0.1\","
            "\"build\":\"Test Build\","
            "\"md5\":\"2F173423615FE972834523192623DF62\""
            "}";
        terminal_print("Testing OTA with MD5 verification...");
        parse_ota_json_and_start(test_ota, strlen(test_ota));
    } else if ((strcmp(cmd, "help") == 0) || (strcmp(cmd, "?") == 0)) {
        terminal_print("Available commands:");
        terminal_print("  set N    - set target temperature");
        terminal_print("  on       - turn on");
        terminal_print("  off      - turn off");
        terminal_print("  info     - republish firmware info");
        terminal_print("  ota      - test OTA (demo)");
    } else {
        terminal_print("Unknown command");
    }
}

void handle_incoming_topic(const char *topic, const char *payload)
{
    LOG_INF("Received: %s -> %s", topic, payload);

    if (strcmp(topic, "downlink/ds/Power") == 0) {
        power_on = atoi(payload);
        publish_str("ds/Set Temperature/prop/isDisabled", power_on ? "0" : "1");
        update_device_simulation();
    } else if (strcmp(topic, "downlink/ds/Set Temperature") == 0) {
        target_temp = atof(payload);
        update_device_simulation();
    } else if (strcmp(topic, "downlink/ds/Terminal") == 0) {
        handle_terminal_command(payload);
        update_device_simulation();
    } else if (strcmp(topic, "downlink/reboot") == 0) {
        LOG_INF("Reboot requested");
        terminal_print("Rebooting...");
        k_sleep(K_SECONDS(1));
        sys_reboot(SYS_REBOOT_COLD);
    } else if (strcmp(topic, "downlink/redirect") == 0) {
        char new_host[128];
        uint16_t new_port;
        if (parse_url(payload, new_host, sizeof(new_host), &new_port)) {
            strncpy(broker_host, new_host, sizeof(broker_host) - 1);
            broker_port = new_port;
            LOG_INF("Redirecting to: %s:%d", broker_host, broker_port);

            abort_mqtt_connection();
            k_sem_give(&mqtt_start);
        } else {
            LOG_ERR("Failed to parse redirect URL: %s", payload);
        }
    } else if (strcmp(topic, "downlink/ota/json") == 0) {
        /* This is the official Blynk OTA notification topic */
        if (blynk_ota_ctx.in_progress) {
            LOG_WRN("OTA already in progress");
            terminal_print("OTA already in progress");
            return;
        }

        LOG_INF("Received Blynk OTA notification");
        terminal_print("Received OTA notification...");

        /* Process the OTA notification according to Blynk specification */
        int ret = parse_ota_json_and_start(payload, strlen(payload));
        if (ret) {
            LOG_ERR("Failed to process OTA notification: %d", ret);
            terminal_print("OTA notification processing failed");
        }
    } else if (strcmp(topic, "downlink/ping") == 0) {
        LOG_DBG("Ping received");
    }
}

/* MQTT connection established callback */
void on_mqtt_connected(void)
{
    /* Send welcome message */
    terminal_print("      ___  __          __");
    terminal_print("     / _ )/ /_ _____  / /__");
    terminal_print("    / _  / / // / _ \\ /  '_/");
    terminal_print("   /____/_/\\_, /_//_/_/\\_\\");
    terminal_print("          /___/");
    terminal_print("Type \"help\" for the list of available commands");

    /* Publish firmware and device info - this is critical for Blynk OTA */
    publish_firmware_info();
}

/* MQTT subscription successful callback */
void on_mqtt_subscribed(void)
{
    publish_str("get/ds", "Power,Set Temperature");

    /* Start periodic device updates */
    k_work_reschedule(&device_update_work, K_SECONDS(get_random_publish_interval()));
}

/* MQTT disconnected callback */
void on_mqtt_disconnected(void)
{
    /* Stop periodic updates */
    k_work_cancel_delayable(&device_update_work);
}

int tls_init(void)
{
    int err = tls_credential_add(APP_CA_CERT_TAG,
                                TLS_CREDENTIAL_CA_CERTIFICATE,
                                ca_certificate,
                                sizeof(ca_certificate));
    if (err < 0) {
        LOG_ERR("Failed to register public certificate: %d", err);
        return err;
    }
    return 0;
}

int main(void)
{
    int rc;

    LOG_INF("Firmware version: %s", CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION);

    /* Initialize work items */
    k_work_init_delayable(&device_update_work, device_update_work_handler);

    /* Confirm boot image - CRITICAL for OTA */
    if (boot_is_img_confirmed() == 0) {
        int ret = boot_write_img_confirmed();
        if (ret == 0) {
            LOG_INF("Image confirmed successfully");
        } else {
            LOG_ERR("Failed to confirm image: %d", ret);
        }
    } else {
        LOG_INF("Image already confirmed");
    }

    /* Check configuration */
    if (strlen(CONFIG_CLOUD_BLYNK_AUTH_TOKEN) == 0) {
        LOG_ERR("Blynk auth token is empty. Check your configuration.");
        return -EINVAL;
    }

    /* Initialize TLS */
    rc = tls_init();
    if (rc) {
        return rc;
    }

    /* Initialize network manager */
    network_manager_init();

    /* Main loop */
    while (1) {
        k_sem_take(&mqtt_start, K_FOREVER);
        connect_to_cloud_and_publish();
    }

    return 0;
}

