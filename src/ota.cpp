#include "ota.h"
#include "config.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "ota";

static char s_url[128] = {};
static char s_key[64] = {};

void ota_init(const char *bridge_url, const char *api_key)
{
    strncpy(s_url, bridge_url, sizeof(s_url) - 1);
    strncpy(s_key, api_key, sizeof(s_key) - 1);
}

// Simple HTTP GET that returns malloc'd response body. Caller frees.
static char *http_get_body(const char *url, int *out_status)
{
    typedef struct { char *data; int len; int cap; } buf_t;
    buf_t buf = {};
    buf.cap = 1024;
    buf.data = (char *)malloc(buf.cap);
    if (!buf.data) return NULL;

    auto handler = [](esp_http_client_event_t *evt) -> esp_err_t {
        buf_t *b = (buf_t *)evt->user_data;
        if (evt->event_id == HTTP_EVENT_ON_DATA) {
            if (b->len + evt->data_len >= b->cap) {
                int nc = b->cap + evt->data_len + 512;
                char *tmp = (char *)realloc(b->data, nc);
                if (!tmp) return ESP_FAIL;
                b->data = tmp;
                b->cap = nc;
            }
            memcpy(b->data + b->len, evt->data, evt->data_len);
            b->len += evt->data_len;
            b->data[b->len] = '\0';
        }
        return ESP_OK;
    };

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = handler;
    config.user_data = &buf;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || *out_status != 200) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

void ota_check_and_update(void)
{
    if (!wifi_is_connected() || s_url[0] == '\0') return;

    // 1. Check for update
    char check_url[256];
    snprintf(check_url, sizeof(check_url), "%s/api/ota/check?v=%s&key=%s", s_url, FW_VERSION, s_key);

    int status = 0;
    char *body = http_get_body(check_url, &status);
    if (!body) {
        ESP_LOGW(TAG, "OTA check failed (HTTP %d)", status);
        return;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return;

    cJSON *update = cJSON_GetObjectItem(root, "update");
    bool has_update = update && cJSON_IsTrue(update);

    cJSON *ver = cJSON_GetObjectItem(root, "version");
    char new_version[32] = {};
    if (ver && cJSON_IsString(ver)) {
        strncpy(new_version, ver->valuestring, sizeof(new_version) - 1);
    }
    cJSON_Delete(root);

    if (!has_update) {
        ESP_LOGI(TAG, "No update available (current: %s)", FW_VERSION);
        return;
    }

    ESP_LOGI(TAG, "Update available: %s → %s. Starting OTA...", FW_VERSION, new_version);

    // 2. Download and flash firmware
    char fw_url[256];
    snprintf(fw_url, sizeof(fw_url), "%s/api/ota/firmware?key=%s", s_url, s_key);

    esp_http_client_config_t ota_config = {};
    ota_config.url = fw_url;
    ota_config.timeout_ms = 60000;
    ota_config.crt_bundle_attach = esp_crt_bundle_attach;
    ota_config.keep_alive_enable = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &ota_config;

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA success! Rebooting...");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }
}
