#ifndef UTILS_H_
#define UTILS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* URL parsing functions */
bool parse_url(const char *url, char *host_out, size_t host_len, uint16_t *port_out);
int parse_url_dynamic(const char *url, char *host_out, size_t host_len,
                      char *path_out, size_t path_len,
                      uint16_t *port_out, bool *use_tls_out);

/* JSON parsing functions */
bool extract_json_number(const char *json, const char *key, uint32_t *out_val);
bool extract_json_string(const char *json, const char *key, char *out_buf, size_t out_len);

/* Common utility macros */
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#endif // UTILS_H_

