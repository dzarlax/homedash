#include "web_identity.h"

#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "nvs.h"

namespace {
constexpr char kNamespace[] = "webidentity";
constexpr char kKey[] = "device_id";
char s_device_id[33] = {};
bool s_creation_failed = false;

bool valid_id(const char *id) {
    if (!id || strlen(id) != 32) return false;
    for (size_t i = 0; i < 32; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f')))
            return false;
    return true;
}

bool read_id(char *out, size_t size) {
    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    char value[40] = {};
    size_t length = sizeof(value);
    esp_err_t result = nvs_get_str(handle, kKey, value, &length);
    nvs_close(handle);
    if (result != ESP_OK || !valid_id(value) || size < sizeof(s_device_id)) return false;
    memcpy(out, value, sizeof(s_device_id));
    return true;
}
}

bool web_identity_init(void) {
    // Always read NVS on explicit initialization (including simulated reboot); a
    // RAM cache must never hide an identity storage error or a changed identity.
    char stored[33] = {};
    if (read_id(stored, sizeof(stored))) {
        memcpy(s_device_id, stored, sizeof(s_device_id));
        return true;
    }
    if (s_creation_failed) return false;

    // A read failure other than a missing key is not permission to replace ID.
    nvs_handle_t handle;
    esp_err_t open_result = nvs_open(kNamespace, NVS_READONLY, &handle);
    if (open_result == ESP_OK) {
        size_t length = 0;
        esp_err_t key_result = nvs_get_str(handle, kKey, nullptr, &length);
        nvs_close(handle);
        if (key_result != ESP_ERR_NVS_NOT_FOUND) return false;
    } else if (open_result != ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }

    // Do not mint another candidate on later retries after any failed create
    // attempt; only a fresh boot may retry when no ID was ever advertised.
    s_creation_failed = true;
    uint8_t random_bytes[16] = {};
    esp_fill_random(random_bytes, sizeof(random_bytes));
    char generated[33] = {};
    for (size_t i = 0; i < sizeof(random_bytes); ++i)
        snprintf(generated + i * 2, 3, "%02x", random_bytes[i]);

    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_str(handle, kKey, generated);
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    if (result != ESP_OK) {
        // Commit errors can be ambiguous. Accept only if a read proves that the
        // stable value was durably written; otherwise never mint again this boot.
        if (!read_id(stored, sizeof(stored))) return false;
        memcpy(s_device_id, stored, sizeof(s_device_id));
        return true;
    }
    memcpy(s_device_id, generated, sizeof(s_device_id));
    return true;
}

bool web_identity_get(char *out, size_t out_size) {
    if (!out || out_size < sizeof(s_device_id) || !valid_id(s_device_id)) return false;
    memcpy(out, s_device_id, sizeof(s_device_id));
    return true;
}

bool web_identity_matches(const char *device_id) {
    return valid_id(s_device_id) && valid_id(device_id) && !strcmp(device_id, s_device_id);
}

esp_err_t web_identity_set_http_header(esp_http_client_handle_t client) {
    if (!client || !valid_id(s_device_id)) return ESP_FAIL;
    return esp_http_client_set_header(client, "X-Device-ID", s_device_id);
}
