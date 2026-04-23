/*
 * Copyright (c) 2020 Circuit Dojo LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
LOG_MODULE_REGISTER(main);

/* nRF Libraries */
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>

/* Local */
#include "cloud/cloud.h"
#include "credentials/credentials.h"

/* ── Blynk firmware binary tag ───────────────────────────────────────────── */

#define BLYNK_PARAM_KV(k, v)  k "\0" v "\0"

volatile const char firmwareTag[] __attribute__((used)) = "blnkinf\0"
	BLYNK_PARAM_KV("mcu"    , CONFIG_FIRMWARE_VERSION)
	BLYNK_PARAM_KV("fw-type", CONFIG_BLYNK_TEMPLATE_ID)
	BLYNK_PARAM_KV("build"  , __DATE__ " " __TIME__)
	BLYNK_PARAM_KV("blynk"  , "0.1.0")
	BLYNK_PARAM_KV("hw"     , CONFIG_BOARD)
	"\0";

/* ── Button ──────────────────────────────────────────────────────────────── */

#define SW0_NODE DT_ALIAS(sw0)
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);
static struct gpio_callback button_cb_data;

/* Semaphore signalled from button ISR, consumed in main loop */
K_SEM_DEFINE(button_sem, 0, 1);

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	k_sem_give(&button_sem);
}

/* ── Timer ───────────────────────────────────────────────────────────────── */

static void timeout_handler(struct k_timer *timer_id);
K_TIMER_DEFINE(timer, timeout_handler, NULL);

K_SEM_DEFINE(publish_sem, 0, 1);

static void timeout_handler(struct k_timer *timer_id)
{
	LOG_INF("Publish interval");
	k_sem_give(&publish_sem);
}

/* ── Variables ───────────────────────────────────────────────────────────── */

static struct device_data data = {
    .do_something = true,
};

void cloud_cb(struct device_data *p_data)
{
    LOG_INF("Cloud callback");
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(void)
{
    int err;

    LOG_INF("Blynk MQTT Sample. Board: %s  FW: %s  Build: %s %s",
            CONFIG_BOARD, CONFIG_FIRMWARE_VERSION, __DATE__, __TIME__);
    (void)firmwareTag[0]; /* retain firmware tag in binary */

    /* ── Button init ─────────────────────────────────────────────────────── */

    if (!gpio_is_ready_dt(&button)) {
        LOG_ERR("Button GPIO not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&button, GPIO_INPUT);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    /* ── Credentials ─────────────────────────────────────────────────────── */

    err = credentials_init();
    if (err == -ENOENT) {
        while (1) {
            k_sleep(K_FOREVER);
        }
    } else if (err < 0) {
        LOG_ERR("Failed to init credentials storage. (err: %i)", err);
        return err;
    }

    /* ── Modem ───────────────────────────────────────────────────────────── */

    err = nrf_modem_lib_init();
    if (err < 0) {
        LOG_ERR("Failed to init modem lib. (err: %i)", err);
        return err;
    }

    /* ── Cloud init ──────────────────────────────────────────────────────── */

    err = cloud_init(cloud_cb);
    if (err < 0) {
        LOG_ERR("Unable to set callback. Err: %i", err);
        return err;
    }

    /* ── LTE ─────────────────────────────────────────────────────────────── */

    LOG_INF("Connecting to cellular network…");
    err = lte_lc_connect();
    if (err < 0) {
        LOG_ERR("Failed to connect. Err: %i", err);
        return err;
    }

    /* ── MQTT ────────────────────────────────────────────────────────────── */

    cloud_start();
    cloud_wait_connected();

    k_timer_start(&timer, K_MINUTES(CONFIG_DEFAULT_DELAY), K_MINUTES(CONFIG_DEFAULT_DELAY));
    k_sem_give(&publish_sem);

    struct k_poll_event events[2] = {
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
            K_POLL_MODE_NOTIFY_ONLY, &publish_sem),
        K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
            K_POLL_MODE_NOTIFY_ONLY, &button_sem),
    };

    while (1) {
        k_poll(events, 2, K_FOREVER);

        if (k_sem_take(&button_sem, K_NO_WAIT) == 0) {
            LOG_INF("Button pressed");
            err = cloud_publish_button();
            if (err < 0) {
                LOG_ERR("Unable to publish button. Err: %i", err);
            }
        }

        if (k_sem_take(&publish_sem, K_NO_WAIT) == 0) {
            err = cloud_publish(&data);
            if (err < 0) {
                LOG_ERR("Unable to publish. Err: %i", err);
            }
        }

        /* Reset events for next poll */
        events[0].state = K_POLL_STATE_NOT_READY;
        events[1].state = K_POLL_STATE_NOT_READY;
    }
}
