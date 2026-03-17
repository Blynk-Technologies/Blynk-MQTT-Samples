#ifndef OTA_MANAGER_H_
#define OTA_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <zephyr/net/http/client.h>

#define APP_CA_CERT_TAG 1

struct ota_context {
    bool in_progress;
    uint32_t size;
    uint32_t downloaded;
    char url[256];
    char version[32];
    char host[128];
    char path[512];
    uint16_t port;
    bool use_tls;
};

extern struct ota_context blynk_ota_ctx;

/* Main OTA functions */
int parse_ota_json_and_start(const char *json_data, size_t json_len);

/* HTTP response callback for OTA */
void ota_http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data);

/* MCUboot functions - make sure these are declared */
extern int boot_set_pending_multi(int image_index, int permanent);
extern bool boot_is_img_confirmed(void);
extern int boot_write_img_confirmed(void);

#endif // OTA_MANAGER_H_

