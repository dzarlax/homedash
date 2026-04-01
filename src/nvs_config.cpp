#include "nvs_config.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "nvs_config";
static device_config_t s_cfg = {};
static bool s_loaded = false;

static bool read_str(nvs_handle_t h, const char *key, char *buf, size_t buf_size)
{
    size_t len = buf_size;
    esp_err_t err = nvs_get_str(h, key, buf, &len);
    if (err == ESP_OK) return true;
    ESP_LOGW(TAG, "NVS key '%s' not found: %s", key, esp_err_to_name(err));
    return false;
}

bool nvs_config_load(device_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace 'config': %s", esp_err_to_name(err));
        return false;
    }

    memset(cfg, 0, sizeof(*cfg));
    bool ok = true;
    ok = read_str(h, "wifi_ssid", cfg->wifi_ssid, sizeof(cfg->wifi_ssid)) && ok;
    ok = read_str(h, "wifi_pass", cfg->wifi_pass, sizeof(cfg->wifi_pass)) && ok;
    ok = read_str(h, "bridge_url", cfg->bridge_url, sizeof(cfg->bridge_url)) && ok;
    ok = read_str(h, "bridge_key", cfg->bridge_key, sizeof(cfg->bridge_key)) && ok;

    nvs_close(h);

    if (ok) {
        memcpy(&s_cfg, cfg, sizeof(s_cfg));
        s_loaded = true;
        ESP_LOGI(TAG, "Config loaded: SSID=%s bridge=%s", cfg->wifi_ssid, cfg->bridge_url);
    } else {
        ESP_LOGE(TAG, "Config incomplete — flash nvs.bin first");
    }
    return ok;
}

const device_config_t *nvs_config_get(void)
{
    return s_loaded ? &s_cfg : NULL;
}

bool nvs_config_set_bridge_url(const char *url)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &h);
    if (err != ESP_OK) return false;

    err = nvs_set_str(h, "bridge_url", url);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        strncpy(s_cfg.bridge_url, url, sizeof(s_cfg.bridge_url) - 1);
        ESP_LOGI(TAG, "Bridge URL updated to %s", url);
        return true;
    }
    ESP_LOGE(TAG, "Failed to update bridge_url: %s", esp_err_to_name(err));
    return false;
}
