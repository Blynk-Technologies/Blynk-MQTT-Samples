/*
 * Copyright (c) 2020 Circuit Dojo LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>

#include "credentials.h"

LOG_MODULE_REGISTER(credentials);

/* NVS key IDs */
#define NVS_ID_SERVER      1
#define NVS_ID_AUTH_TOKEN  2
#define NVS_ID_TEMPLATE_ID 3

static struct nvs_fs fs;

static char server[CRED_SERVER_MAX_LEN];
static char auth_token[CRED_AUTH_TOKEN_MAX_LEN];
static char template_id[CRED_TEMPLATE_MAX_LEN];

/* ── NVS init ────────────────────────────────────────────────────────────── */

static int nvs_storage_init(void)
{
	const struct flash_area *fa;
	int err;

	err = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
	if (err) {
		LOG_ERR("Failed to open storage partition: %d", err);
		return err;
	}

	fs.flash_device = flash_area_get_device(fa);
	if (!device_is_ready(fs.flash_device)) {
		LOG_ERR("Flash device not ready");
		flash_area_close(fa);
		return -ENODEV;
	}

	fs.offset       = fa->fa_off;
	fs.sector_size  = 4096;
	fs.sector_count = (uint16_t)(fa->fa_size / fs.sector_size);

	flash_area_close(fa);

	err = nvs_mount(&fs);
	if (err) {
		LOG_ERR("nvs_mount() failed: %d", err);
	}
	return err;
}

static void read_nvs_str(uint16_t id, char *buf, size_t buf_size)
{
	ssize_t rc = nvs_read(&fs, id, buf, buf_size);

	if (rc <= 0) {
		buf[0] = '\0';
	}
}

/* ── credentials_init ────────────────────────────────────────────────────── */

int credentials_init(void)
{
	int err = nvs_storage_init();

	if (err) {
		return err;
	}

	read_nvs_str(NVS_ID_SERVER,      server,      sizeof(server));
	read_nvs_str(NVS_ID_AUTH_TOKEN,  auth_token,  sizeof(auth_token));
	read_nvs_str(NVS_ID_TEMPLATE_ID, template_id, sizeof(template_id));

	/* Apply compile-time defaults for optional fields if not stored */
	if (server[0] == '\0') {
		strncpy(server, CONFIG_BLYNK_SERVER, sizeof(server) - 1);
	}

	if (auth_token[0] == '\0') {
		LOG_WRN("No auth token stored. Use shell to provision:");
		LOG_WRN("  cred token <your-auth-token>");
		LOG_WRN("  cred server <host>      (optional, default: %s)", CONFIG_BLYNK_SERVER);
		LOG_WRN("  cred template <id>      (optional)");
		LOG_WRN("  cred show               (show current values)");
		LOG_WRN("  cred clear              (erase all credentials)");
		return -ENOENT;
	}

	LOG_INF("Credentials loaded:");
	LOG_INF("  Server:      %s", server);
	LOG_INF("  Template ID: %s", template_id[0] ? template_id : "(none)");
	LOG_INF("  Auth token:  %.8s…", auth_token);
	return 0;
}

/* ── Accessors ───────────────────────────────────────────────────────────── */

const char *credentials_get_server(void)      { return server; }
const char *credentials_get_auth_token(void)  { return auth_token; }
const char *credentials_get_template_id(void) { return template_id; }

/* ── Shell commands ──────────────────────────────────────────────────────── */

static int cmd_cred_token(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: cred token <auth-token>");
		return -EINVAL;
	}
	strncpy(auth_token, argv[1], sizeof(auth_token) - 1);
	auth_token[sizeof(auth_token) - 1] = '\0';
	nvs_write(&fs, NVS_ID_AUTH_TOKEN, auth_token, strlen(auth_token) + 1);
	shell_print(sh, "Auth token saved.");
	return 0;
}

static int cmd_cred_server(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: cred server <host>");
		return -EINVAL;
	}
	strncpy(server, argv[1], sizeof(server) - 1);
	server[sizeof(server) - 1] = '\0';
	nvs_write(&fs, NVS_ID_SERVER, server, strlen(server) + 1);
	shell_print(sh, "Server saved: %s", server);
	return 0;
}

static int cmd_cred_template(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_error(sh, "Usage: cred template <template-id>");
		return -EINVAL;
	}
	strncpy(template_id, argv[1], sizeof(template_id) - 1);
	template_id[sizeof(template_id) - 1] = '\0';
	nvs_write(&fs, NVS_ID_TEMPLATE_ID, template_id, strlen(template_id) + 1);
	shell_print(sh, "Template ID saved: %s", template_id);
	return 0;
}

static int cmd_cred_show(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Server:      %s", server[0] ? server : "(not set)");
	shell_print(sh, "Template ID: %s", template_id[0] ? template_id : "(not set)");
	shell_print(sh, "Auth token:  %s", auth_token[0] ? auth_token : "(not set)");
	return 0;
}

static int cmd_cred_clear(const struct shell *sh, size_t argc, char **argv)
{
	nvs_delete(&fs, NVS_ID_SERVER);
	nvs_delete(&fs, NVS_ID_AUTH_TOKEN);
	nvs_delete(&fs, NVS_ID_TEMPLATE_ID);
	server[0] = auth_token[0] = template_id[0] = '\0';
	shell_print(sh, "Credentials cleared. Rebooting…");
	k_sleep(K_MSEC(200));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(cred_cmds,
	SHELL_CMD_ARG(token,    NULL, "Set Blynk auth token (reboots)",    cmd_cred_token,    2, 0),
	SHELL_CMD_ARG(server,   NULL, "Set Blynk server host",             cmd_cred_server,   2, 0),
	SHELL_CMD_ARG(template, NULL, "Set Blynk template ID",             cmd_cred_template, 2, 0),
	SHELL_CMD_ARG(show,     NULL, "Show current credentials",          cmd_cred_show,     1, 0),
	SHELL_CMD_ARG(clear,    NULL, "Erase all credentials (reboots)",   cmd_cred_clear,    1, 0),
	SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(cred, &cred_cmds, "Blynk credential management", NULL);
