/*
 * SPDX-FileCopyrightText: 2025 Khrystyna Olkhovetska for Blynk Technologies Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(utils, LOG_LEVEL_INF);

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include "utils.h"
#include <errno.h>

// Simple URL parser for broker redirection with better validation
bool parse_url(const char *url, char *host_out, size_t host_len, uint16_t *port_out)
{
    if (!url || !host_out || !port_out || host_len == 0) {
        return false;
    }

    const char *p = strstr(url, "://");
    if (!p) {
        LOG_ERR("No protocol in URL");
        return false;
    }
    p += 3;

    // Check if there's anything after protocol
    if (*p == '\0') {
        LOG_ERR("Empty host in URL");
        return false;
    }

    const char *colon = strchr(p, ':');
    const char *slash = strchr(p, '/');

    size_t host_size;
    if (colon && (!slash || colon < slash)) {
        host_size = (size_t)(colon - p);
        if (host_size >= host_len) {
            LOG_ERR("Host too long: %zu chars", host_size);
            return false;
        }
        memcpy(host_out, p, host_size);
        host_out[host_size] = '\0';

        // Parse port with validation
        char *endptr;
        unsigned long port = strtoul(colon + 1, &endptr, 10);
        if (port == 0 || port > 65535) {
            LOG_ERR("Invalid port: %lu", port);
            return false;
        }
        *port_out = (uint16_t)port;
    } else {
        host_size = slash ? (size_t)(slash - p) : strlen(p);
        if (host_size >= host_len) {
            LOG_ERR("Host too long: %zu chars", host_size);
            return false;
        }
        memcpy(host_out, p, host_size);
        host_out[host_size] = '\0';

        // Default port based on protocol
        if (strncmp(url, "mqtts://", 8) == 0) {
            *port_out = 8883;
        } else if (strncmp(url, "mqtt://", 7) == 0) {
            *port_out = 1883;
        } else {
            *port_out = 9443; // Default Blynk port
        }
    }

    // Validate hostname is not empty
    if (strlen(host_out) == 0) {
        LOG_ERR("Empty hostname");
        return false;
    }

    return true;
}

bool extract_json_number(const char *json, const char *key, uint32_t *out_val)
{
    if (!json || !key || !out_val) {
        return false;
    }

    char *p = strstr(json, key);
    if (!p) {
        return false;
    }

    // Find the colon after the key
    p = strchr(p, ':');
    if (!p) {
        return false;
    }
    p++;

    // Skip whitespace and quotes
    while (*p && (isspace((unsigned char)*p) || *p == '"')) {
        p++;
    }

    if (!*p || !isdigit((unsigned char)*p)) {
        return false;
    }

    // Parse the number more carefully
    char *endptr;
    unsigned long val = strtoul(p, &endptr, 10);

    // Validate the parsing
    if (endptr == p) {
        LOG_ERR("No digits found for %s", key);
        return false;
    }

    if (val > UINT32_MAX) {
        LOG_ERR("Value too large for %s: %lu", key, val);
        return false;
    }

    *out_val = (uint32_t)val;
    LOG_DBG("Parsed %s: %u", key, *out_val);
    return true;
}

bool extract_json_string(const char *json, const char *key, char *out_buf, size_t out_len)
{
    if (!json || !key || !out_buf || out_len == 0) {
        return false;
    }

    const char *json_end = json + strlen(json);
    char *p = strstr(json, key);
    if (!p || p >= json_end) {
        LOG_ERR("Key %s not found in JSON", key);
        return false;
    }

    p = strchr(p, ':');
    if (!p || p >= json_end) {
        LOG_ERR("Colon not found after key %s", key);
        return false;
    }

    // Skip whitespace after colon
    p++;
    while (p < json_end && *p && isspace((unsigned char)*p)) {
        p++;
    }

    if (p >= json_end) {
        LOG_ERR("Unexpected end of JSON after key %s", key);
        return false;
    }

    // Find opening quote
    p = strchr(p, '"');
    if (!p || p >= json_end) {
        LOG_ERR("Opening quote not found for key %s", key);
        return false;
    }
    p++;

    // Find closing quote with bounds checking
    char *q = p;
    while (q < json_end && *q && *q != '"') {
        // Handle escaped quotes
        if (*q == '\\' && (q + 1) < json_end && *(q + 1) == '"') {
            q += 2;
        } else {
            q++;
        }
    }

    if (q >= json_end || *q != '"') {
        LOG_ERR("Closing quote not found for key %s", key);
        return false;
    }

    size_t len = (size_t)(q - p);
    if (len >= out_len) {
        LOG_WRN("String too long for buffer, truncating");
        len = out_len - 1;
    }

    memcpy(out_buf, p, len);
    out_buf[len] = '\0';
    LOG_DBG("Parsed %s: %s", key, out_buf);
    return true;
}


int parse_url_dynamic(const char *url, char *host_out, size_t host_len,
                      char *path_out, size_t path_len,
                      uint16_t *port_out, bool *use_tls_out)
{
    if (!url || !host_out || !path_out || !port_out || !use_tls_out) {
        return -EINVAL;
    }

    LOG_INF("Parsing URL: %s", url);

    const char *scheme = strstr(url, "://");
    if (!scheme) {
        LOG_ERR("No scheme found in URL");
        return -EINVAL;
    }

    if (strncmp(url, "https", 5) == 0) {
        *use_tls_out = true;
        *port_out = 443;
    } else if (strncmp(url, "http", 4) == 0) {
        *use_tls_out = false;
        *port_out = 80;
    } else {
        LOG_ERR("Unsupported scheme");
        return -EINVAL;
    }

    scheme += 3; // Skip past "://"

    const char *slash = strchr(scheme, '/');
    if (!slash) {
        slash = scheme + strlen(scheme);
        if (path_len < 2) {
            return -ENAMETOOLONG;
        }
        strncpy(path_out, "/", path_len);
    } else {
        size_t path_size = strlen(slash);
        if (path_size >= path_len) {
            LOG_ERR("Path too long");
            return -ENAMETOOLONG;
        }
        strncpy(path_out, slash, path_len);
    }
    path_out[path_len - 1] = '\0';

    size_t host_size = (size_t)(slash - scheme);
    if (host_size >= host_len) {
        LOG_ERR("Host too long");
        return -ENAMETOOLONG;
    }

    char temp_host[128];
    if (host_size >= sizeof(temp_host)) {
        return -ENAMETOOLONG;
    }

    memcpy(temp_host, scheme, host_size);
    temp_host[host_size] = '\0';

    // Remove port if present
    char *colon = strchr(temp_host, ':');
    if (colon) {
        *colon = '\0';
        char *endptr;
        unsigned long port = strtoul(colon + 1, &endptr, 10);
        if (port == 0 || port > 65535 || *endptr != '\0') {
            LOG_ERR("Invalid port number in URL: %s", colon + 1);
            return -EINVAL;
        }
        *port_out = (uint16_t)port;
    }

    // Normalize any subdomain like fra1.blynk.cloud to blynk.cloud
    const char *suffix = ".blynk.cloud";
    size_t suffix_len = strlen(suffix);
    size_t temp_len = strlen(temp_host);
    if (temp_len > suffix_len &&
        strcmp(temp_host + temp_len - suffix_len, suffix) == 0) {
        strncpy(host_out, "blynk.cloud", host_len);
    } else {
        strncpy(host_out, temp_host, host_len);
    }

    host_out[host_len - 1] = '\0';

    LOG_INF("Parsed - Host: %s, Port: %u, Path: %s, TLS: %s",
            host_out, *port_out, path_out, *use_tls_out ? "yes" : "no");

    return 0;
}

