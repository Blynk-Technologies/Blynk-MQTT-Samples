/*
 * SPDX-FileCopyrightText: 2025 Khrystyna Olkhovetska for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ota, LOG_LEVEL_INF);

#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/crypto/crypto.h>
#include <mbedtls/sha1.h>
#include <mbedtls/md5.h>

#include "ota_manager.h"
#include "mqtt_client.h"
#include "root_certificates.h"
#include "utils.h"

/* Blynk firmware identification macro */
#define BLYNK_PARAM_KV(k, v)    k "\0" v "\0"

struct ota_context blynk_ota_ctx = {0};

#define OTA_BUFFER_SIZE      2048
#define OTA_MAX_URL_LEN      512
#define OTA_MAX_VERSION_LEN  32
#define OTA_TIMEOUT_MS       (60 * MSEC_PER_SEC)
#define OTA_MIN_FIRMWARE_SIZE 1024      // Minimum valid firmware size
#define OTA_MAX_FIRMWARE_SIZE (512*1024) // Maximum firmware size
#define SHA1_DIGEST_SIZE     20
#define MD5_DIGEST_SIZE      16

static uint8_t ota_recv_buf[OTA_BUFFER_SIZE];

struct ota_download_state {
    const struct flash_area *fa_slot1;
    uint32_t offset;
    bool flash_opened;
    bool headers_skipped;
    bool download_complete;
    int last_error;

    /* CRC32 verification */
    uint32_t crc32;
    uint32_t expected_crc32;
    bool crc32_expected;

    /* MD5 verification */
    mbedtls_md5_context md5_ctx;
    bool md5_initialized;
    uint8_t expected_md5[MD5_DIGEST_SIZE];
    bool md5_expected;

    /* SHA1 verification */
    mbedtls_sha1_context sha1_ctx;
    bool sha1_initialized;
    uint8_t expected_sha1[SHA1_DIGEST_SIZE];
    bool sha1_expected;

    /* Firmware info from Blynk headers */
    char fw_type[32];
    char fw_version[32];
    char fw_build[64];
};

static struct ota_download_state download_state = {0};

/* Blynk firmware tag */
static void ota_init_firmware_tag(void)
{
    volatile const char firmwareTag[] = "blnkinf\0"
    BLYNK_PARAM_KV("mcu"    , CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION)
    BLYNK_PARAM_KV("fw-type", CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE)
    BLYNK_PARAM_KV("ver"    , CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION)
    BLYNK_PARAM_KV("build"  , __DATE__ " " __TIME__)
    "\0";
    (void)firmwareTag;
}

static void reset_download_state(void)
{
    if (download_state.flash_opened && download_state.fa_slot1) {
        flash_area_close(download_state.fa_slot1);
    }
    if (download_state.sha1_initialized) {
        mbedtls_sha1_free(&download_state.sha1_ctx);
    }
    if (download_state.md5_initialized) {
        mbedtls_md5_free(&download_state.md5_ctx);
    }
    memset(&download_state, 0, sizeof(download_state));
}

/* Parse hex string to binary */
static int parse_hex_string(const char *hex_str, uint8_t *output, size_t expected_len)
{
    if (!hex_str || !output) {
        return -EINVAL;
    }

    size_t hex_len = strlen(hex_str);
    if (hex_len != expected_len * 2) {
        LOG_ERR("Invalid hex string length: %zu, expected %zu", hex_len, expected_len * 2);
        return -EINVAL;
    }

    for (size_t i = 0; i < expected_len; i++) {
        char hex_byte[3] = {0};
        hex_byte[0] = hex_str[i * 2];
        hex_byte[1] = hex_str[i * 2 + 1];

        char *endptr;
        output[i] = (uint8_t)strtol(hex_byte, &endptr, 16);
        if (*endptr != '\0') {
            LOG_ERR("Invalid hex character at position %zu", i * 2);
            return -EINVAL;
        }
    }
    return 0;
}

/* Parse Blynk x-md5 header (HEX-encoded MD5) */
static int parse_blynk_md5_header(const char *headers, uint8_t *md5_out)
{
    if (!headers || !md5_out) {
        return -EINVAL;
    }

    const char *md5_header = strstr(headers, "x-md5:");
    if (!md5_header) {
        md5_header = strstr(headers, "X-MD5:");
    }
    if (!md5_header) {
        LOG_DBG("No x-md5 header found");
        return -ENOENT;
    }

    // Skip header name and find colon
    const char *md5_start = strchr(md5_header, ':');
    if (!md5_start) {
        return -EINVAL;
    }
    md5_start++;

    // Skip whitespace
    while (*md5_start && isspace((unsigned char)*md5_start)) {
        md5_start++;
    }

    // Extract 32 hex characters for MD5
    char md5_hex[33] = {0};
    int hex_count = 0;
    for (int i = 0; i < 32 && md5_start[i]; i++) {
        if (isxdigit((unsigned char)md5_start[i])) {
            md5_hex[hex_count++] = md5_start[i];
        } else if (md5_start[i] == '\r' || md5_start[i] == '\n' || md5_start[i] == ' ') {
            break;  // End of MD5 value
        } else {
            LOG_ERR("Invalid MD5 hex character: %c", md5_start[i]);
            return -EINVAL;
        }
    }

    if (hex_count != 32) {
        LOG_ERR("Invalid MD5 hex length: %d, expected 32", hex_count);
        return -EINVAL;
    }

    int ret = parse_hex_string(md5_hex, md5_out, MD5_DIGEST_SIZE);
    if (ret == 0) {
        LOG_INF("Blynk x-md5 header parsed: %s", md5_hex);
        LOG_HEXDUMP_INF(md5_out, MD5_DIGEST_SIZE, "Expected MD5:");
    }
    return ret;
}

/* Parse firmware info from Blynk headers */
static void parse_blynk_firmware_headers(const char *headers)
{
    const char *start, *end;
    size_t len;

    /* Parse x-fw-type */
    const char *fw_type = strstr(headers, "x-fw-type:");
    if (!fw_type) fw_type = strstr(headers, "X-FW-TYPE:");
    if (fw_type) {
        start = strchr(fw_type, ':') + 1;
        while (*start && isspace((unsigned char)*start)) start++;
        end = start;
        while (*end && !isspace((unsigned char)*end) && *end != '\r' && *end != '\n') end++;
        len = MIN(end - start, sizeof(download_state.fw_type) - 1);
        memcpy(download_state.fw_type, start, len);
        download_state.fw_type[len] = '\0';
        LOG_INF("OTA Firmware type: %s", download_state.fw_type);
    }

    /* Parse x-fw-ver */
    const char *fw_ver = strstr(headers, "x-fw-ver:");
    if (!fw_ver) fw_ver = strstr(headers, "X-FW-VER:");
    if (fw_ver) {
        start = strchr(fw_ver, ':') + 1;
        while (*start && isspace((unsigned char)*start)) start++;
        end = start;
        while (*end && !isspace((unsigned char)*end) && *end != '\r' && *end != '\n') end++;
        len = MIN(end - start, sizeof(download_state.fw_version) - 1);
        memcpy(download_state.fw_version, start, len);
        download_state.fw_version[len] = '\0';
        LOG_INF("OTA Firmware version: %s", download_state.fw_version);
    }

    /* Parse x-fw-build */
    const char *fw_build = strstr(headers, "x-fw-build:");
    if (!fw_build) fw_build = strstr(headers, "X-FW-BUILD:");
    if (fw_build) {
        start = strchr(fw_build, ':') + 1;
        while (*start && isspace((unsigned char)*start)) start++;
        end = start;
        while (*end && *end != '\r' && *end != '\n') end++;
        len = MIN(end - start, sizeof(download_state.fw_build) - 1);
        memcpy(download_state.fw_build, start, len);
        download_state.fw_build[len] = '\0';
        LOG_INF("OTA Firmware build: %s", download_state.fw_build);
    }
}

/* Verify firmware CRC32 */
static int verify_firmware_crc32(void)
{
    if (!download_state.crc32_expected) {
        LOG_INF("No CRC32 verification required");
        return 0;
    }

    if (download_state.crc32 != download_state.expected_crc32) {
        LOG_ERR("CRC32 verification failed!");
        LOG_ERR("Calculated: 0x%08x, Expected: 0x%08x",
                download_state.crc32, download_state.expected_crc32);
        return -EBADMSG;
    }

    LOG_INF("CRC32 verification successful: 0x%08x", download_state.crc32);
    return 0;
}

/* Verify firmware MD5 */
static int verify_firmware_md5(void)
{
    if (!download_state.md5_initialized || !download_state.md5_expected) {
        LOG_INF("No MD5 verification required");
        return 0;
    }

    uint8_t calculated_md5[MD5_DIGEST_SIZE];
    int ret = mbedtls_md5_finish(&download_state.md5_ctx, calculated_md5);
    if (ret != 0) {
        LOG_ERR("Failed to finalize MD5: %d", ret);
        return -EIO;
    }

    if (memcmp(calculated_md5, download_state.expected_md5, MD5_DIGEST_SIZE) != 0) {
        LOG_ERR("MD5 verification failed!");
        LOG_HEXDUMP_ERR(calculated_md5, MD5_DIGEST_SIZE, "Calculated:");
        LOG_HEXDUMP_ERR(download_state.expected_md5, MD5_DIGEST_SIZE, "Expected:");
        return -EBADMSG;
    }

    LOG_INF("MD5 verification successful!");
    LOG_HEXDUMP_INF(calculated_md5, MD5_DIGEST_SIZE, "MD5:");
    return 0;
}

static int verify_firmware_sha1(void)
{
    if (!download_state.sha1_initialized || !download_state.sha1_expected) {
        LOG_INF("No SHA1 verification required");
        return 0;
    }

    uint8_t calculated_sha1[SHA1_DIGEST_SIZE];
    int ret = mbedtls_sha1_finish(&download_state.sha1_ctx, calculated_sha1);
    if (ret != 0) {
        LOG_ERR("Failed to finalize SHA1: %d", ret);
        return -EIO;
    }

    if (memcmp(calculated_sha1, download_state.expected_sha1, SHA1_DIGEST_SIZE) != 0) {
        LOG_ERR("SHA1 verification failed!");
        LOG_HEXDUMP_ERR(calculated_sha1, SHA1_DIGEST_SIZE, "Calculated:");
        LOG_HEXDUMP_ERR(download_state.expected_sha1, SHA1_DIGEST_SIZE, "Expected:");
        return -EBADMSG;
    }

    LOG_INF("SHA1 verification successful");
    return 0;
}

static void ota_progress_callback(uint32_t downloaded, uint32_t total)
{
    static uint32_t last_percent = 0;

    if (total == 0) {
        return;
    }

    uint32_t percent = (downloaded * 100) / total;

    if (percent >= last_percent + 10) {
        last_percent = percent;
        LOG_INF("OTA Progress: %u%% (%u/%u bytes)", percent, downloaded, total);
    }
}

static int get_tls_socket_and_connect(const char *host, uint16_t port)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    char port_str[8];
    int sock, ret;

    snprintf(port_str, sizeof(port_str), "%u", port);

    ret = getaddrinfo(host, port_str, &hints, &res);
    if (ret != 0) {
        LOG_ERR("getaddrinfo failed: %s", gai_strerror(ret));
        return -EIO;
    }

    // Create TLS socket using native API
    sock = socket(res->ai_family, res->ai_socktype, IPPROTO_TLS_1_2);
    if (sock < 0) {
        LOG_ERR("socket failed: %d", errno);
        freeaddrinfo(res);
        return -errno;
    }

    // Set TLS options
    sec_tag_t sec_tag_opt[] = { APP_CA_CERT_TAG };
    ret = setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
                     sec_tag_opt, sizeof(sec_tag_opt));
    if (ret < 0) {
        LOG_ERR("Failed to set security tag: %d", errno);
        close(sock);
        freeaddrinfo(res);
        return -errno;
    }

    ret = setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host));
    if (ret < 0) {
        LOG_ERR("Failed to set hostname: %d", errno);
        close(sock);
        freeaddrinfo(res);
        return -errno;
    }

    // Connect
    ret = connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    if (ret < 0) {
        LOG_ERR("connect failed: %d", errno);
        close(sock);
        return -errno;
    }

    return sock;
}

static int get_tcp_socket_and_connect(const char *host, uint16_t port)
{
    struct zsock_addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
        .ai_protocol = IPPROTO_TCP,
    };
    struct zsock_addrinfo *res = NULL;
    int sock, ret;
    char port_str[8];

    snprintf(port_str, sizeof(port_str), "%u", port);

    ret = zsock_getaddrinfo(host, port_str, &hints, &res);
    if (ret != 0) {
        LOG_ERR("zsock_getaddrinfo failed: %d", ret);
        return -EIO;
    }

    sock = zsock_socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) {
        LOG_ERR("zsock_socket failed: %d", sock);
        zsock_freeaddrinfo(res);
        return sock;
    }

    struct zsock_timeval timeout = {
        .tv_sec = 30,
        .tv_usec = 0
    };
    zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    ret = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
    zsock_freeaddrinfo(res);

    if (ret < 0) {
        LOG_ERR("zsock_connect() failed: %d", ret);
        zsock_close(sock);
        return ret;
    }

    return sock;
}

void ota_http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    int ret;

    LOG_DBG("HTTP callback: status=%d, recv_len=%zu, final=%d",
            rsp->http_status_code, rsp->recv_buf_len, final_data);

    if (!blynk_ota_ctx.in_progress) {
        LOG_WRN("Received HTTP callback but OTA not in progress");
        return;
    }

    // Check for reasonable download size limits
    if (blynk_ota_ctx.size > 0 && blynk_ota_ctx.downloaded > blynk_ota_ctx.size * 1.2) {
        LOG_ERR("Download size exceeded limit, aborting");
        download_state.last_error = -EFBIG;
        blynk_ota_ctx.in_progress = false;
        terminal_print("OTA Error: Download size exceeded");
        return;
    }

    if (rsp->http_status_code == 0 && final_data == HTTP_DATA_FINAL && rsp->recv_buf_len == 0) {
        LOG_ERR("HTTP connection failed or no response received");
        download_state.last_error = -ECONNRESET;
        blynk_ota_ctx.in_progress = false;
        terminal_print("OTA Error: Connection failed");
        return;
    }

    if (rsp->http_status_code != 0 && rsp->http_status_code != 200) {
        LOG_ERR("HTTP error: %d", rsp->http_status_code);
        download_state.last_error = -EPROTO;
        blynk_ota_ctx.in_progress = false;
        terminal_print("OTA Error: HTTP error");
        return;
    }

    // Check for Blynk OTA headers
    if (rsp->http_status_code == 200 && !download_state.flash_opened) {
        LOG_INF("Received HTTP 200 OK, checking for Blynk OTA headers");

        if (rsp->recv_buf_len > 0) {
            char *header_end = strstr((char*)rsp->recv_buf, "\r\n\r\n");
            if (header_end) {
                *header_end = '\0'; // Temporarily null-terminate headers

                // Parse Blynk firmware information
                parse_blynk_firmware_headers((char*)rsp->recv_buf);

                // Look for Blynk x-md5 header (HEX-encoded MD5)
                ret = parse_blynk_md5_header((char*)rsp->recv_buf, download_state.expected_md5);
                if (ret == 0) {
                    download_state.md5_expected = true;
                    LOG_INF("Will verify MD5 checksum from x-md5 header");
                }

                *header_end = '\r'; // Restore header
            }
        }
    }

    if (!download_state.flash_opened) {
        LOG_INF("Opening flash area for OTA update");

        ret = flash_area_open(FIXED_PARTITION_ID(slot1_partition), &download_state.fa_slot1);
        if (ret) {
            LOG_ERR("Failed to open flash area: %d", ret);
            download_state.last_error = ret;
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: Flash open failed");
            return;
        }

        LOG_INF("Erasing flash area (size: %zu bytes)", download_state.fa_slot1->fa_size);
        terminal_print("Erasing flash...");

        ret = flash_area_erase(download_state.fa_slot1, 0, download_state.fa_slot1->fa_size);
        if (ret) {
            LOG_ERR("Failed to erase flash area: %d", ret);
            flash_area_close(download_state.fa_slot1);
            download_state.last_error = ret;
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: Flash erase failed");
            return;
        }

        // Initialize CRC32 calculation
        download_state.crc32 = 0;

        // Initialize MD5 context if we have expected hash
        if (download_state.md5_expected) {
            mbedtls_md5_init(&download_state.md5_ctx);
            ret = mbedtls_md5_starts(&download_state.md5_ctx);
            if (ret != 0) {
                LOG_ERR("Failed to initialize MD5: %d", ret);
                download_state.md5_expected = false;
            } else {
                download_state.md5_initialized = true;
                LOG_INF("MD5 verification initialized");
            }
        }

        // Initialize SHA1 context if we have expected hash
        if (download_state.sha1_expected) {
            mbedtls_sha1_init(&download_state.sha1_ctx);
            ret = mbedtls_sha1_starts(&download_state.sha1_ctx);
            if (ret != 0) {
                LOG_ERR("Failed to initialize SHA1: %d", ret);
                download_state.sha1_expected = false;
            } else {
                download_state.sha1_initialized = true;
                LOG_INF("SHA1 verification initialized");
            }
        }

        download_state.flash_opened = true;
        download_state.headers_skipped = false;
        download_state.offset = 0;
        blynk_ota_ctx.downloaded = 0;

        LOG_INF("Flash area ready, starting download...");
        terminal_print("Downloading firmware...");
    }

    if (rsp->recv_buf_len > 0) {
        uint8_t *data_start = rsp->recv_buf;
        uint32_t data_len = rsp->recv_buf_len;

        // Skip HTTP headers on first packet
        if (!download_state.headers_skipped) {
            uint8_t *header_end = NULL;

            // Look for end of headers (double CRLF: \r\n\r\n)
            for (uint32_t i = 0; i < rsp->recv_buf_len - 3; i++) {
                if (rsp->recv_buf[i] == '\r' && rsp->recv_buf[i+1] == '\n' &&
                    rsp->recv_buf[i+2] == '\r' && rsp->recv_buf[i+3] == '\n') {
                    header_end = &rsp->recv_buf[i + 4];
                    break;
                }
            }

            if (header_end) {
                data_start = header_end;
                data_len = rsp->recv_buf_len - (header_end - rsp->recv_buf);
                download_state.headers_skipped = true;
                LOG_INF("HTTP headers skipped, firmware data length: %u bytes", data_len);
            } else {
                LOG_DBG("Headers not complete yet, waiting for more data");
                return;
            }
        }

        // Write firmware data to flash
        if (data_len > 0) {
            if (download_state.offset + data_len > download_state.fa_slot1->fa_size) {
                LOG_ERR("Firmware too large: offset=%u, data_len=%u, flash_size=%zu",
                        download_state.offset, data_len, download_state.fa_slot1->fa_size);
                download_state.last_error = -EFBIG;
                blynk_ota_ctx.in_progress = false;
                terminal_print("OTA Error: Firmware too large");
                return;
            }

            ret = flash_area_write(download_state.fa_slot1, download_state.offset, data_start, data_len);
            if (ret) {
                LOG_ERR("Failed to write to flash at offset %u: %d", download_state.offset, ret);
                download_state.last_error = ret;
                blynk_ota_ctx.in_progress = false;
                terminal_print("OTA Error: Flash write failed");
                return;
            }

            // Update CRC32 calculation
            download_state.crc32 = crc32_ieee_update(download_state.crc32, data_start, data_len);

            // Update MD5 if verification is enabled
            if (download_state.md5_initialized) {
                ret = mbedtls_md5_update(&download_state.md5_ctx, data_start, data_len);
                if (ret != 0) {
                    LOG_ERR("Failed to update MD5: %d", ret);
                    download_state.md5_initialized = false;
                }
            }

            // Update SHA1 if verification is enabled
            if (download_state.sha1_initialized) {
                ret = mbedtls_sha1_update(&download_state.sha1_ctx, data_start, data_len);
                if (ret != 0) {
                    LOG_ERR("Failed to update SHA1: %d", ret);
                    download_state.sha1_initialized = false;
                }
            }

            download_state.offset += data_len;
            blynk_ota_ctx.downloaded += data_len;

            if (blynk_ota_ctx.size > 0) {
                ota_progress_callback(blynk_ota_ctx.downloaded, blynk_ota_ctx.size);
            } else {
                if (blynk_ota_ctx.downloaded % 4096 == 0) {
                    LOG_INF("Downloaded: %u bytes", blynk_ota_ctx.downloaded);
                }
            }
        }
    }

    if (final_data == HTTP_DATA_FINAL) {
        LOG_INF("HTTP transfer completed");

        if (download_state.flash_opened) {
            flash_area_close(download_state.fa_slot1);
            download_state.flash_opened = false;
        }

        if (blynk_ota_ctx.downloaded < OTA_MIN_FIRMWARE_SIZE) {
            LOG_ERR("Downloaded firmware too small: %u bytes", blynk_ota_ctx.downloaded);
            download_state.last_error = -EINVAL;
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: Invalid firmware size");
            return;
        }

        // Verify CRC32 if available
        ret = verify_firmware_crc32();
        if (ret != 0) {
            LOG_ERR("CRC32 verification failed: %d", ret);
            download_state.last_error = ret;
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: CRC32 verification failed");
            return;
        }

        // Verify MD5 if available
        ret = verify_firmware_md5();
        if (ret != 0) {
            LOG_ERR("MD5 verification failed: %d", ret);
            download_state.last_error = ret;
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: MD5 verification failed");
            return;
        }

        // Verify SHA1 if available
        ret = verify_firmware_sha1();
        if (ret != 0) {
            LOG_ERR("SHA1 verification failed: %d", ret);
            download_state.last_error = ret;
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: SHA1 verification failed");
            return;
        }

        LOG_INF("OTA download completed successfully: %u bytes", blynk_ota_ctx.downloaded);
        LOG_INF("Final CRC32: 0x%08x", download_state.crc32);
        download_state.download_complete = true;
        blynk_ota_ctx.in_progress = false;

        ret = boot_set_pending_multi(0, 0);
        if (ret) {
            LOG_ERR("boot_set_pending failed: %d", ret);
            terminal_print("OTA Error: Failed to set pending");
            return;
        }

        terminal_print("OTA Success! Rebooting in 5 seconds...");
        LOG_INF("New firmware marked as pending. Rebooting in 5 seconds...");
        k_sleep(K_MSEC(5000));
        sys_reboot(SYS_REBOOT_COLD);
    }
}

/* Check if update is feasible */
static bool is_update_feasible(const char *json_data)
{
    char new_version[32] = {0};
    char new_type[32] = {0};
    uint32_t new_size = 0;

    /* Extract update info */
    extract_json_string(json_data, "\"ver\"", new_version, sizeof(new_version));
    extract_json_string(json_data, "\"type\"", new_type, sizeof(new_type));
    extract_json_number(json_data, "\"size\"", &new_size);

    LOG_INF("Update feasibility check:");
    LOG_INF("  Current: type=%s, ver=%s", CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE, CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION);
    LOG_INF("  New: type=%s, ver=%s, size=%u", new_type, new_version, new_size);

    /* Check firmware type compatibility */
    if (strlen(new_type) > 0 && strcmp(CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE, new_type) != 0) {
        LOG_WRN("Firmware type mismatch: current=%s, new=%s", CONFIG_CLOUD_BLYNK_FIRMWARE_TYPE, new_type);
        return false;
    }

    /* Check if it's the same version */
    if (strlen(new_version) > 0 && strcmp(CONFIG_CLOUD_BLYNK_FIRMWARE_VERSION, new_version) == 0) {
        LOG_INF("Same firmware version, skipping update");
        return false;
    }

    /* Check size constraints */
    if (new_size > 0 && (new_size < OTA_MIN_FIRMWARE_SIZE || new_size > OTA_MAX_FIRMWARE_SIZE)) {
        LOG_ERR("Invalid firmware size: %u bytes", new_size);
        return false;
    }

    LOG_INF("Update is feasible");
    return true;
}

int parse_ota_json_and_start(const char *json_data, size_t json_len)
{
    char url_buf[OTA_MAX_URL_LEN] = {0};
    uint32_t size_val = 0;
    char ver_buf[OTA_MAX_VERSION_LEN] = {0};
    char type_buf[32] = {0};
    char build_buf[64] = {0};
    char md5_buf[64] = {0};  /* Buffer for MD5 checksum from JSON */
    int ret;

    LOG_INF("Parsing Blynk OTA JSON (length: %zu)", json_len);
    LOG_INF("OTA JSON: %.*s", (int)json_len, json_data);

    /* Check if update is feasible before starting */
    if (!is_update_feasible(json_data)) {
        LOG_INF("Update not feasible, skipping");
        terminal_print("OTA update not needed");
        return 0;
    }

    /* Initialize firmware tag */
    ota_init_firmware_tag();
    reset_download_state();

    if (!extract_json_string(json_data, "\"url\"", url_buf, sizeof(url_buf))) {
        LOG_ERR("Failed to parse URL from OTA JSON");
        terminal_print("OTA Error: Invalid JSON - no URL");
        return -EINVAL;
    }

    extract_json_number(json_data, "\"size\"", &size_val);
    extract_json_string(json_data, "\"ver\"", ver_buf, sizeof(ver_buf));
    extract_json_string(json_data, "\"type\"", type_buf, sizeof(type_buf));
    extract_json_string(json_data, "\"build\"", build_buf, sizeof(build_buf));

    /* Parse checksums from JSON if provided */
    if (extract_json_string(json_data, "\"md5\"", md5_buf, sizeof(md5_buf))) {
        LOG_INF("Found MD5 in JSON: %s", md5_buf);
        ret = parse_hex_string(md5_buf, download_state.expected_md5, MD5_DIGEST_SIZE);
        if (ret == 0) {
            download_state.md5_expected = true;
            LOG_INF("Will verify MD5 checksum from JSON");
            LOG_HEXDUMP_INF(download_state.expected_md5, MD5_DIGEST_SIZE, "Expected MD5:");
        } else {
            LOG_WRN("Invalid MD5 format in JSON, ignoring");
        }
    }

    /* Validate firmware size */
    if (size_val > 0 && (size_val < OTA_MIN_FIRMWARE_SIZE || size_val > OTA_MAX_FIRMWARE_SIZE)) {
        LOG_ERR("Invalid firmware size: %u bytes", size_val);
        terminal_print("OTA Error: Invalid firmware size");
        return -EINVAL;
    }

    strncpy(blynk_ota_ctx.url, url_buf, sizeof(blynk_ota_ctx.url) - 1);
    blynk_ota_ctx.url[sizeof(blynk_ota_ctx.url) - 1] = '\0';
    blynk_ota_ctx.size = size_val;
    strncpy(blynk_ota_ctx.version, ver_buf, sizeof(blynk_ota_ctx.version) - 1);
    blynk_ota_ctx.version[sizeof(blynk_ota_ctx.version) - 1] = '\0';

    LOG_INF("OTA package info:");
    LOG_INF("  URL: %s", url_buf);
    LOG_INF("  Size: %u bytes", size_val);
    LOG_INF("  Version: %s", ver_buf);
    LOG_INF("  Type: %s", type_buf);
    LOG_INF("  Build: %s", build_buf);

    ret = parse_url_dynamic(blynk_ota_ctx.url,
                           blynk_ota_ctx.host, sizeof(blynk_ota_ctx.host),
                           blynk_ota_ctx.path, sizeof(blynk_ota_ctx.path),
                           &blynk_ota_ctx.port, &blynk_ota_ctx.use_tls);
    if (ret) {
        LOG_ERR("Failed to parse URL \"%s\": %d", blynk_ota_ctx.url, ret);
        terminal_print("OTA Error: Invalid URL");
        return ret;
    }

    blynk_ota_ctx.in_progress = true;
    blynk_ota_ctx.downloaded = 0;

    LOG_INF("Starting OTA download:");
    LOG_INF("  URL: %s", blynk_ota_ctx.url);
    LOG_INF("  Host: %s", blynk_ota_ctx.host);
    LOG_INF("  Port: %u", blynk_ota_ctx.port);
    LOG_INF("  Path: %s", blynk_ota_ctx.path);
    LOG_INF("  TLS: %s", blynk_ota_ctx.use_tls ? "enabled" : "disabled");
    LOG_INF("  Expected size: %u bytes", blynk_ota_ctx.size);
    LOG_INF("  Version: %s", blynk_ota_ctx.version);

    if (download_state.md5_expected) {
        LOG_HEXDUMP_INF(download_state.expected_md5, MD5_DIGEST_SIZE, "Expected MD5:");
    }

    terminal_print("Starting OTA download...");

    int sock = -1;
    if (blynk_ota_ctx.use_tls) {
        sock = get_tls_socket_and_connect(blynk_ota_ctx.host, blynk_ota_ctx.port);
        if (sock < 0) {
            LOG_ERR("Failed to establish TLS connection: %d", sock);
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: TLS connection failed");
            return sock;
        }
    } else {
        sock = get_tcp_socket_and_connect(blynk_ota_ctx.host, blynk_ota_ctx.port);
        if (sock < 0) {
            LOG_ERR("Failed to establish TCP connection: %d", sock);
            blynk_ota_ctx.in_progress = false;
            terminal_print("OTA Error: TCP connection failed");
            return sock;
        }
    }

    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", blynk_ota_ctx.port);

    // Add User-Agent and other headers that some servers expect
    static const char *headers[] = {
        "User-Agent: Zephyr-Blynk-OTA/1.0\r\n",
        "Accept: */*\r\n",
        "Connection: close\r\n",
        "Cache-Control: no-cache\r\n",
        NULL
    };

    struct http_request ota_request = {
        .method       = HTTP_GET,
        .url          = blynk_ota_ctx.path,
        .host         = blynk_ota_ctx.host,
        .port         = port_str,
        .protocol     = "HTTP/1.1",
        .header_fields = headers,
        .response     = ota_http_response_cb,
        .recv_buf     = ota_recv_buf,
        .recv_buf_len = sizeof(ota_recv_buf),
    };

    LOG_INF("Sending HTTP GET request...");
    LOG_DBG("Request details: %s %s HTTP/1.1", "GET", blynk_ota_ctx.path);
    LOG_DBG("Host: %s:%s", blynk_ota_ctx.host, port_str);

    ret = http_client_req(sock, &ota_request, OTA_TIMEOUT_MS, NULL);

    if (ret < 0) {
        LOG_ERR("HTTP request failed: %d", ret);
        zsock_close(sock);
        blynk_ota_ctx.in_progress = false;
        reset_download_state();
        terminal_print("OTA Error: HTTP request failed");
        return ret;
    }

    LOG_INF("HTTP request initiated successfully");

    int wait_count = 0;
    while (blynk_ota_ctx.in_progress && wait_count < 120) { // Wait up to 2 minutes
        k_sleep(K_MSEC(1000));
        wait_count++;

        if (wait_count % 10 == 0) {
            LOG_INF("OTA still in progress... (%d seconds)", wait_count);
        }
    }

    zsock_close(sock);

    if (blynk_ota_ctx.in_progress) {
        LOG_ERR("OTA download timed out");
        blynk_ota_ctx.in_progress = false;
        reset_download_state();
        terminal_print("OTA Error: Download timeout");
        return -ETIMEDOUT;
    }

    if (download_state.last_error != 0) {
        LOG_ERR("OTA download failed with error: %d", download_state.last_error);
        return download_state.last_error;
    }

    if (download_state.download_complete) {
        LOG_INF("OTA download completed successfully");
        return 0;
    }

    LOG_ERR("OTA download ended unexpectedly");
    terminal_print("OTA Error: Unexpected end");
    return -EIO;
}

