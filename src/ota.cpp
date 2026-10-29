#include "ota.h"
#include "config.h"
#include "wifi_manager.h"
#include "photo_frame.h"
#include "web_identity.h"

#include <stdio.h>
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
static bool s_update_available = false;
static char s_available_version[32] = {};

static esp_err_t ota_http_client_init(esp_http_client_handle_t client)
{
    esp_err_t result = esp_http_client_set_header(client, "X-API-Key", s_key);
    if (result != ESP_OK) return result;
    return web_identity_set_http_header(client);
}

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
    esp_http_client_set_header(client, "X-API-Key", s_key);
    if (web_identity_set_http_header(client) != ESP_OK) {
        esp_http_client_cleanup(client);
        free(buf.data);
        return NULL;
    }
    esp_err_t err = esp_http_client_perform(client);
    *out_status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || *out_status != 200) {
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

bool ota_check_available(bool *available)
{
    if (available) *available = false;
    s_update_available = false;
    s_available_version[0] = 0;
    if (!wifi_is_connected() || s_url[0] == '\0') return false;

    // 1. Check for update
    char check_url[256];
    snprintf(check_url, sizeof(check_url), "%s/api/ota/check?v=%s", s_url, FW_VERSION);

    int status = 0;
    char *body = http_get_body(check_url, &status);
    if (!body) {
        ESP_LOGW(TAG, "OTA check failed (HTTP %d)", status);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) return false;

    cJSON *update = cJSON_GetObjectItem(root, "update");
    bool has_update = update && cJSON_IsTrue(update);

    cJSON *ver = cJSON_GetObjectItem(root, "version");
    char new_version[32] = {};
    if (ver && cJSON_IsString(ver)) {
        strncpy(new_version, ver->valuestring, sizeof(new_version) - 1);
    }
    cJSON_Delete(root);
    s_update_available = has_update;
    strncpy(s_available_version, new_version, sizeof(s_available_version) - 1);
    if (available) *available = has_update;
    if (!has_update) ESP_LOGI(TAG, "No update available (current: %s)", FW_VERSION);
    else ESP_LOGI(TAG, "Update available: %s → %s", FW_VERSION, new_version);
    return true;
}

bool ota_install_available(void)
{
    if (!s_update_available || !wifi_is_connected() || s_url[0] == '\0') return false;
    ESP_LOGI(TAG, "Starting managed OTA install: %s", s_available_version);
    // Pause the photo worker only when a real firmware download will start.
    if (!photo_frame_pause_for_ota()) {
        ESP_LOGW(TAG, "Photo worker is busy; postponing OTA");
        return false;
    }

    // 2. Download and flash firmware
    char fw_url[256];
    snprintf(fw_url, sizeof(fw_url), "%s/api/ota/firmware", s_url);

    esp_http_client_config_t ota_config = {};
    ota_config.url = fw_url;
    ota_config.timeout_ms = 60000;
    ota_config.crt_bundle_attach = esp_crt_bundle_attach;
    ota_config.keep_alive_enable = true;

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &ota_config;
    ota_cfg.http_client_init_cb = ota_http_client_init;

    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA image installed; caller will reboot");
        s_update_available = false;
        photo_frame_resume_after_ota();
        return true;
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    }
    photo_frame_resume_after_ota();
    return false;
}

const char *ota_available_version(void) {
    return s_update_available ? s_available_version : "";
}

void ota_check_and_update(void)
{
    bool available = false;
    if (!ota_check_available(&available) || !available) return;
    if (ota_install_available()) esp_restart();
}
