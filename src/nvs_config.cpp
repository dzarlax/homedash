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
        ESP_LOGI(TAG, "Config loaded");
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
        ESP_LOGI(TAG, "Bridge URL updated");
        return true;
    }
    ESP_LOGE(TAG, "Failed to update bridge_url: %s", esp_err_to_name(err));
    return false;
}

bool nvs_config_set_credentials(const device_config_t *cfg)
{
    if (!cfg || !cfg->wifi_ssid[0] || strlen(cfg->wifi_ssid) > 32 ||
        strlen(cfg->wifi_pass) > 64 || !cfg->bridge_url[0] ||
        strlen(cfg->bridge_url) >= sizeof(cfg->bridge_url) ||
        !cfg->bridge_key[0] || strlen(cfg->bridge_key) >= sizeof(cfg->bridge_key) ||
        (strncmp(cfg->bridge_url, "https://", 8) && strncmp(cfg->bridge_url, "http://", 7)))
        return false;
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &h);
    if (err != ESP_OK) return false;
    err = nvs_set_str(h, "wifi_ssid", cfg->wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(h, "wifi_pass", cfg->wifi_pass);
    if (err == ESP_OK) err = nvs_set_str(h, "bridge_url", cfg->bridge_url);
    if (err == ESP_OK) err = nvs_set_str(h, "bridge_key", cfg->bridge_key);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        memcpy(&s_cfg, cfg, sizeof(s_cfg));
        s_loaded = true;
        return true;
    }
    ESP_LOGW(TAG, "Credential update failed (%s)", esp_err_to_name(err));
    return false;
}

bool nvs_config_reset_device(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("config", NVS_READWRITE, &h);
    if (err != ESP_OK) return false;
    const char *keys[] = {"wifi_ssid", "wifi_pass", "bridge_url", "bridge_key"};
    for (const char *key : keys) {
        err = nvs_erase_key(h, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        if (err != ESP_OK) break;
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK) {
        memset(&s_cfg, 0, sizeof(s_cfg));
        s_loaded = false;
        return true;
    }
    ESP_LOGW(TAG, "Device config reset failed (%s)", esp_err_to_name(err));
    return false;
}
