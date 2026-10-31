#include "brightness_runtime.h"
#include "web_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <memory>
#include <new>
#include "esp_tls.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_config.h"
#include "ota.h"
#include "photo_frame.h"
#include "lvgl_lock.h"
#include "ui_dashboard.h"
#include "web_settings_store.h"
#include "web_identity.h"
#include "wifi_manager.h"

namespace {
constexpr size_t kMaxResponse = 8192;
constexpr size_t kMaxRequest = 3072;
constexpr uint32_t kMaxRevision = 0x7fffffff;
struct response_buffer_t {
    char data[kMaxResponse + 1];
    size_t used;
    bool overflow;
    int tls_error;
    int tls_code;
    int tls_flags;
};
// Only the network task writes. Publish the mutex before the UI can read it.
static std::atomic<SemaphoreHandle_t> s_diagnostic_mutex{nullptr};
static char s_diagnostic[96] = {};
static void set_diagnostic(const char *message) {
    auto mutex = s_diagnostic_mutex.load(std::memory_order_acquire);
    if (!mutex) {
        mutex = xSemaphoreCreateMutex();
        if (!mutex) return;
        s_diagnostic_mutex.store(mutex, std::memory_order_release);
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(s_diagnostic, sizeof(s_diagnostic), "%s", message);
    xSemaphoreGive(mutex);
}
static std::unique_ptr<response_buffer_t> allocate_response() {
    std::unique_ptr<response_buffer_t> response(new (std::nothrow) response_buffer_t{});
    if (!response) set_diagnostic("Sync:response OOM");
    return response;
}
static char s_sync_error[48] = {};
// Old Bridges reject unknown heartbeat fields. Learn support from a validated
// response, scope it to the peer, and relearn after an HTTP 400 on rollback.
static bool s_locale_report_supported = false;
static bool s_backlight_report_supported = false;
static bool s_optional_telemetry_supported = true;
static char s_locale_peer[128] = {};
static char s_locale_peer_key[64] = {};
static void select_locale_peer(const char *url, const char *key) {
    if (!url || !key || strcmp(s_locale_peer, url) || strcmp(s_locale_peer_key, key)) {
        s_locale_report_supported = false;
        s_backlight_report_supported = false;
        s_optional_telemetry_supported = true;
        snprintf(s_locale_peer, sizeof(s_locale_peer), "%s", url ? url : "");
        snprintf(s_locale_peer_key, sizeof(s_locale_peer_key), "%s", key ? key : "");
    }
}

static esp_err_t receive_event(esp_http_client_event_t *event) {
    response_buffer_t *buffer = static_cast<response_buffer_t *>(event->user_data);
    if (event->event_id == HTTP_EVENT_ERROR && event->data) {
        buffer->tls_error = esp_tls_get_and_clear_last_error(
            static_cast<esp_tls_error_handle_t>(event->data),
            &buffer->tls_code, &buffer->tls_flags);
    }
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t size = static_cast<size_t>(event->data_len);
        if (size > kMaxResponse - buffer->used) {
            buffer->overflow = true;
            return ESP_FAIL;
        }
        memcpy(buffer->data + buffer->used, event->data, size);
        buffer->used += size;
        buffer->data[buffer->used] = 0;
    }
    return ESP_OK;
}

static void add_string(cJSON *object, const char *key, const char *value) {
    cJSON_AddStringToObject(object, key, value ? value : "");
}

static cJSON *make_payload(const char *firmware, const char *boot_id,
                           const char *result_id, const char *result_state,
                           const char *result_error) {
    if (!web_settings_store_sync_allowed()) return nullptr;
    cJSON *root = cJSON_CreateObject();
    if (!root) return nullptr;
    add_string(root, "firmware", firmware);
    add_string(root, "boot_id", boot_id);
    char device_id[33] = {};
    if (!web_identity_get(device_id, sizeof(device_id))) {
        cJSON_Delete(root);
        return nullptr;
    }
    add_string(root, "device_id", device_id);
    if (s_sync_error[0]) add_string(root, "error", s_sync_error);
    if (ota_available_version()[0]) add_string(root, "available_version", ota_available_version());
    web_preferences_t applied = {};
    add_string(root, "bridge_id", web_settings_store_load(&applied) ? applied.bridge_id : "");
    if (s_locale_report_supported)
        add_string(root, "applied_locale", web_settings_store_locale_confirmed(&applied) ? applied.locale : "");
    bool brightness_ready = false; char brightness_error[24] = {};
    if (lvgl_port_lock(-1)) {
        brightness_ready = brightness_runtime_ready();
        snprintf(brightness_error, sizeof(brightness_error), "%s", brightness_runtime_error());
        lvgl_port_unlock();
    }
    if (s_optional_telemetry_supported && brightness_error[0]) add_string(root, "backlight_error", brightness_error);
    if (s_backlight_report_supported) {
        const auto report = brightness_runtime_report();
        cJSON *b = cJSON_AddObjectToObject(root, "backlight_report");
        cJSON_AddNumberToObject(b, "version", 1);
        add_string(b, "phase", report.phase);
        cJSON_AddBoolToObject(b, "output_known", report.output_known);
        if (report.has_last_command) cJSON_AddNumberToObject(b, "last_command", report.last_command);
        else cJSON_AddNullToObject(b, "last_command");
        cJSON_AddNumberToObject(b, "sample_uptime_ms", (double)report.sample_uptime_ms);
        if (report.has_test) {
            cJSON_AddNumberToObject(b, "test_command", report.test_command);
            cJSON_AddNumberToObject(b, "return_command", report.return_command);
            cJSON_AddNumberToObject(b, "restore_deadline_uptime_ms", (double)report.restore_deadline_uptime_ms);
        }
        add_string(b, "error", report.error);
    }
    bool needs_brightness = applied.backlight_percent || applied.night_backlight_percent || applied.night_dim;
    cJSON_AddNumberToObject(root, "applied_revision",
                            web_settings_store_favorites_confirmed(&applied) && (!needs_brightness || (brightness_ready && !brightness_error[0])) ? applied.revision : 0);
    char ota_id[65] = {}, ota_stage[16] = {}, ota_error[24] = {};
    if (s_optional_telemetry_supported && web_settings_store_ota_telemetry(ota_id, sizeof(ota_id), ota_stage, sizeof(ota_stage), ota_error, sizeof(ota_error))) {
        add_string(root, "ota_attempt_id", ota_id);
        if (ota_stage[0]) add_string(root, "ota_stage", ota_stage);
        if (ota_error[0]) add_string(root, "ota_error", ota_error);
    }
    cJSON_AddNumberToObject(root, "uptime_seconds", esp_timer_get_time() / 1000000);
    cJSON_AddBoolToObject(root, "time_synced", wifi_time_is_valid());
    cJSON_AddStringToObject(root, "model", "Waveshare ESP32-S3-Touch-LCD-7B");
    cJSON_AddNumberToObject(root, "width", 1024);
    cJSON_AddNumberToObject(root, "height", 600);
    char ip[16] = "";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {};
    if (netif && esp_netif_get_ip_info(netif, &info) == ESP_OK)
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
    add_string(root, "ip", ip);
    wifi_ap_record_t ap = {};
    char ssid[33] = "";
    if (wifi_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        memcpy(ssid, ap.ssid, sizeof(ap.ssid));
        ssid[sizeof(ssid) - 1] = 0;
    }
    add_string(root, "ssid", ssid);
    cJSON *caps = cJSON_AddArrayToObject(root, "capabilities");
    static const char *capabilities[] = {
        "photo_scene_unified",
        "home_favorites", "ota_history_details", "preferences", "photo_start", "photo_overlay", "photo_overlay_adaptive", "restart", "ota_check", "ota_install", "reset", "network", "locale"
    };
    for (const char *cap : capabilities) cJSON_AddItemToArray(caps, cJSON_CreateString(cap));
    // Runtime is unconfigured by default: capability requires a verified baseline.
    {
        bool ready = brightness_ready;
        if (ready) { cJSON_AddItemToArray(caps, cJSON_CreateString("backlight_brightness")); cJSON_AddItemToArray(caps, cJSON_CreateString("backlight_preview")); cJSON_AddItemToArray(caps, cJSON_CreateString("backlight_restore")); }
    }
    if (result_id && result_state) {
        cJSON *result = cJSON_AddObjectToObject(root, "command_result");
        add_string(result, "id", result_id);
        add_string(result, "state", result_state);
        if (result_error && result_error[0]) add_string(result, "error", result_error);
    } else {
        char id[65] = {}, state[16] = {}, error[96] = {};
        if (web_settings_store_command_result(id, sizeof(id), state, sizeof(state),
                                             error, sizeof(error))) {
            cJSON *result = cJSON_AddObjectToObject(root, "command_result");
            add_string(result, "id", id);
            add_string(result, "state", state);
            if (error[0]) add_string(result, "error", error);
        }
    }
    return root;
}

static bool post_payload(const char *url, const char *key, const char *payload,
                         response_buffer_t *response, int *status_out) {
    if (!url || !url[0] || !key || !key[0] || !payload || !response) return false;
    esp_http_client_config_t config = {};
    char endpoint[192];
    if (snprintf(endpoint, sizeof(endpoint), "%s/api/device/v1/sync", url) >=
        static_cast<int>(sizeof(endpoint))) return false;
    config.url = endpoint;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = receive_event;
    config.user_data = response;
    config.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { set_diagnostic("Sync:client OOM"); return false; }
    esp_http_client_set_header(client, "X-API-Key", key);
    if (web_identity_set_http_header(client) != ESP_OK) {
        set_diagnostic("Sync:identity unavailable");
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_http_client_set_post_field(client, payload, strlen(payload));
    // Keep the last known result while an ordinary heartbeat is in flight.
    char previous[96];
    if (!web_sync_copy_diagnostic(previous, sizeof(previous))) set_diagnostic("Sync:connecting");
    esp_err_t err = esp_http_client_perform(client);
    int socket_error = esp_http_client_get_errno(client);
    int status = esp_http_client_get_status_code(client);
    char diagnostic[96] = {};
    if (response->overflow) {
        snprintf(diagnostic, sizeof(diagnostic), "Sync:response too large");
    } else if (err != ESP_OK) {
        snprintf(diagnostic, sizeof(diagnostic), "Sync:%s tls=%x/%x/%x e=%d",
                 esp_err_to_name(err), static_cast<unsigned>(response->tls_error),
                 static_cast<unsigned>(response->tls_code),
                 static_cast<unsigned>(response->tls_flags), socket_error);
    } else if (status != 200) {
        snprintf(diagnostic, sizeof(diagnostic), "Sync:HTTP %d", status);
    }
    if (diagnostic[0]) set_diagnostic(diagnostic);
    if (err == ESP_OK && status == 400) { s_locale_report_supported = false; s_backlight_report_supported = false; s_optional_telemetry_supported = false; }
    if (status_out) *status_out = status;
    esp_http_client_cleanup(client);
    return err == ESP_OK && status == 200 && !response->overflow;
}

static char *print_payload(cJSON *root) {
    if (!root) return nullptr;
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body || strlen(body) > kMaxRequest) { free(body); return nullptr; }
    return body;
}

static bool prepare_preferences(cJSON *incoming, uint32_t revision,
                                const char *bridge_id, web_preferences_t *out) {
    if (!cJSON_IsObject(incoming) || !out || !revision || revision > kMaxRevision)
        return false;
    cJSON *screen = cJSON_GetObjectItemCaseSensitive(incoming, "screen");
    cJSON *posix = cJSON_GetObjectItemCaseSensitive(incoming, "posix_timezone");
    cJSON *settings_url = cJSON_GetObjectItemCaseSensitive(incoming, "settings_url");
    if (!cJSON_IsObject(screen) || !cJSON_IsString(posix) || !cJSON_IsString(settings_url)) {
        return false;
    }
    char *json = cJSON_PrintUnformatted(incoming);
    if (!json) return false;
    bool valid = web_preferences_parse(json, strlen(json), revision, bridge_id,
                                       posix->valuestring,
                                       settings_url->valuestring, out);
    free(json);
    return valid;
}

static bool apply_received_preferences(const web_preferences_t *next) {
    if (!next || !web_preferences_valid(next)) return false;
    web_preferences_t current = {};
    bool has_current = web_settings_store_load(&current);
    bool brightness_changed = next->backlight_percent || next->night_backlight_percent || next->night_dim;
    if (brightness_changed) {
        if (!lvgl_port_lock(-1)) return false;
        bool ready = brightness_runtime_ready(); lvgl_port_unlock();
        if (!ready) return false;
    }
    if (has_current && next->revision == current.revision &&
        !strcmp(next->bridge_id, current.bridge_id) &&
        !strcmp(next->locale, current.locale) && web_settings_store_locale_confirmed(&current) &&
        web_settings_store_favorites_confirmed(&current) &&
        next->favorite_count == current.favorite_count &&
        !memcmp(next->favorites, current.favorites, sizeof(next->favorites)) &&
        next->backlight_percent == current.backlight_percent && next->night_backlight_percent == current.night_backlight_percent &&
        next->night_dim == current.night_dim) return true;
    if (!web_preferences_revision_allowed(&current, has_current, next->revision,
                                         next->bridge_id)) return false;
    if (!lvgl_port_lock(-1)) return false;
    brightness_checkpoint_t brightness_before = {};
    if (brightness_changed) {
        if (!brightness_runtime_ready() || !brightness_runtime_checkpoint(&brightness_before)) {
            lvgl_port_unlock(); return false;
        }
        if (!ui_dashboard_apply_brightness(next)) {
            brightness_runtime_rollback(&brightness_before);
            brightness_runtime_mark_write_failed();
            lvgl_port_unlock(); return false;
        }
    }
    ui_dashboard_apply_preferences(next);
    lvgl_port_unlock();
    photo_frame_apply_web_preferences(next->photos_enabled, next->photo_idle_seconds,
                                      next->photo_interval_seconds);
    bool saved = web_settings_store_save(next);
    bool rollback_failed = false;
    if (!saved && brightness_changed) {
        if (lvgl_port_lock(-1)) {
            rollback_failed = !brightness_runtime_rollback(&brightness_before);
            lvgl_port_unlock();
        } else rollback_failed = true;
        if (rollback_failed) web_sync_set_error("backlight_error");
    }
    if (!saved && has_current) {
        if (lvgl_port_lock(-1)) {
            if (rollback_failed) ui_dashboard_reset_preferences();
            else ui_dashboard_apply_preferences(&current);
            lvgl_port_unlock();
        }
        photo_frame_apply_web_preferences(current.photos_enabled, current.photo_idle_seconds,
                                          current.photo_interval_seconds);
    } else if (!saved) {
        if (lvgl_port_lock(-1)) {
            ui_dashboard_reset_preferences();
            lvgl_port_unlock();
        }
        photo_frame_apply_web_preferences(true, 300, 60);
    }
    return saved;
}

static bool extract_command(cJSON *response, web_command_t *out) {
    if (!out) return true;
    memset(out, 0, sizeof(*out));
    cJSON *command = cJSON_GetObjectItemCaseSensitive(response, "command");
    if (!command || cJSON_IsNull(command)) return true;
    if (!cJSON_IsObject(command)) return false;
    cJSON *id = cJSON_GetObjectItemCaseSensitive(command, "id");
    cJSON *action = cJSON_GetObjectItemCaseSensitive(command, "action");
    cJSON *expires = cJSON_GetObjectItemCaseSensitive(command, "expires_at");
    if (!cJSON_IsString(id) || !cJSON_IsString(action) || !cJSON_IsString(expires) ||
        strlen(id->valuestring) >= sizeof(out->id) ||
        strlen(action->valuestring) >= sizeof(out->action) ||
        strlen(expires->valuestring) >= sizeof(out->expires_at) ||
        !web_command_id_valid(id->valuestring)) return false;
    static const char *actions[] = {"photo_start", "restart", "ota_check", "ota_install", "reset", "backlight_preview", "backlight_restore"};
    bool known = false;
    for (const char *candidate : actions) if (!strcmp(action->valuestring, candidate)) known = true;
    if (!known) return false;
    cJSON *percent = cJSON_GetObjectItemCaseSensitive(command, "percent");
    if (!strcmp(action->valuestring, "backlight_preview")) {
        if (!cJSON_IsNumber(percent) || percent->valuedouble != percent->valueint ||
            percent->valueint < 10 || percent->valueint > 100) return false;
        out->percent = (uint8_t)percent->valueint;
    } else if (percent && !cJSON_IsNull(percent)) return false;
    cJSON *target = cJSON_GetObjectItemCaseSensitive(command, "target_version");
    if (target && (!cJSON_IsString(target) || !target->valuestring ||
        strlen(target->valuestring) >= sizeof(out->target_version) ||
        strcmp(action->valuestring, "ota_install"))) return false;
    if (target) strcpy(out->target_version, target->valuestring);
    strcpy(out->id, id->valuestring);
    strcpy(out->action, action->valuestring);
    strcpy(out->expires_at, expires->valuestring);
    return true;
}

static bool extract_network(cJSON *preferences, web_network_update_t *out) {
    if (!out) return true;
    memset(out, 0, sizeof(*out));
    cJSON *network = cJSON_GetObjectItemCaseSensitive(preferences, "network");
    if (!network || cJSON_IsNull(network)) return true;
    if (!cJSON_IsObject(network)) return false;
    struct field_t { const char *key; char *value; size_t capacity; bool *present; } fields[] = {
        {"wifi_ssid", out->wifi_ssid, sizeof(out->wifi_ssid), &out->has_wifi_ssid},
        {"wifi_password", out->wifi_password, sizeof(out->wifi_password), &out->has_wifi_password},
        {"bridge_url", out->bridge_url, sizeof(out->bridge_url), &out->has_bridge_url},
        {"bridge_key", out->bridge_key, sizeof(out->bridge_key), &out->has_bridge_key},
    };
    for (const field_t &field : fields) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(network, field.key);
        if (!item) continue;
        if (!cJSON_IsString(item) || !item->valuestring) return false;
        size_t n = strlen(item->valuestring);
        if (n >= field.capacity) return false;
        if (!n) continue; // Redacted/empty secret means keep the installed value.
        memcpy(field.value, item->valuestring, n + 1);
        *field.present = true;
    }
    if (out->has_wifi_ssid && out->has_wifi_password == false) {
        // Password is filled from the existing config by the caller.
    }
    if (out->has_bridge_url && strncmp(out->bridge_url, "http://", 7) &&
        strncmp(out->bridge_url, "https://", 8)) return false;
    return true;
}
}

bool web_sync_poll(const char *url, const char *key, const char *firmware,
                   const char *boot_id, web_sync_response_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    select_locale_peer(url, key);
    cJSON *root = make_payload(firmware, boot_id, nullptr, nullptr, nullptr);
    char *payload = print_payload(root);
    if (!payload) { set_diagnostic("Sync:payload unavailable"); return false; }
    auto response = allocate_response();
    if (!response) { free(payload); return false; }
    int status = 0;
    bool ok = post_payload(url, key, payload, response.get(), &status);
    free(payload);
    if (!ok) return false;
    cJSON *decoded = cJSON_ParseWithLength(response->data, response->used);
    if (!decoded) { set_diagnostic("Sync:invalid JSON"); return false; }
    cJSON *revision = cJSON_GetObjectItemCaseSensitive(decoded, "revision");
    cJSON *device_id = cJSON_GetObjectItemCaseSensitive(decoded, "device_id");
    cJSON *bridge_id = cJSON_GetObjectItemCaseSensitive(decoded, "bridge_id");
    cJSON *prefs = cJSON_GetObjectItemCaseSensitive(decoded, "preferences");
    ok = cJSON_IsString(device_id) && web_identity_matches(device_id->valuestring) &&
         cJSON_IsNumber(revision) && revision->valuedouble == revision->valueint &&
         revision->valueint > 0 && static_cast<uint32_t>(revision->valueint) <= kMaxRevision &&
         cJSON_IsString(bridge_id) && cJSON_IsObject(prefs) && extract_command(decoded, &out->command) &&
         extract_network(prefs, &out->network) &&
         prepare_preferences(prefs, static_cast<uint32_t>(revision->valueint),
                             bridge_id->valuestring, &out->preferences);
    if (ok) {
        out->has_preferences = true;
        web_preferences_t current = {};
        bool has_current = web_settings_store_load(&current);
        const bool has_network_update = out->network.has_wifi_ssid ||
            out->network.has_wifi_password || out->network.has_bridge_url ||
            out->network.has_bridge_key;
        const bool same_scope = has_current &&
            !strcmp(out->preferences.bridge_id, current.bridge_id);
        if (!web_preferences_revision_allowed(&current, has_current,
                out->preferences.revision, out->preferences.bridge_id) ||
            (same_scope && has_network_update &&
             out->preferences.revision == current.revision)) {
            web_sync_set_error("stale_revision");
            ok = false;
        }
    }
    if (ok) {
        cJSON *screen = cJSON_GetObjectItemCaseSensitive(prefs, "screen");
        s_locale_report_supported = cJSON_HasObjectItem(screen, "locale");
        cJSON *backlight_version = cJSON_GetObjectItemCaseSensitive(decoded, "backlight_report_version");
        s_backlight_report_supported = cJSON_IsNumber(backlight_version) && backlight_version->valuedouble == 1;
    }
    cJSON_Delete(decoded);
    if (ok) s_sync_error[0] = 0;
    else memset(out, 0, sizeof(*out));
    set_diagnostic(ok ? "Sync:OK" : "Sync:invalid envelope");
    return ok;
}

bool web_sync_apply_preferences(const web_sync_response_t *response) {
    return response && response->has_preferences &&
           apply_received_preferences(&response->preferences);
}

bool web_sync_probe_bridge(const char *url, const char *key,
                           char *bridge_id, size_t bridge_id_size) {
    if (!url || !key || !bridge_id || bridge_id_size < 33) return false;
    esp_http_client_config_t config = {};
    char endpoint[192];
    if (snprintf(endpoint, sizeof(endpoint), "%s/api/device/v1/probe", url) >=
        static_cast<int>(sizeof(endpoint))) return false;
    config.url = endpoint;
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = receive_event;
    auto response = allocate_response();
    if (!response) return false;
    config.user_data = response.get();
    config.disable_auto_redirect = true;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { set_diagnostic("Sync:client OOM"); return false; }
    esp_http_client_set_header(client, "X-API-Key", key);
    if (web_identity_set_http_header(client) != ESP_OK) {
        set_diagnostic("Sync:identity unavailable");
        esp_http_client_cleanup(client);
        return false;
    }
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || response->overflow) return false;
    cJSON *decoded = cJSON_ParseWithLength(response->data, response->used);
    if (!decoded) { set_diagnostic("Sync:invalid JSON"); return false; }
    cJSON *version = cJSON_GetObjectItemCaseSensitive(decoded, "version");
    cJSON *id = cJSON_GetObjectItemCaseSensitive(decoded, "bridge_id");
    bool valid = cJSON_IsNumber(version) && version->valuedouble == 1.0 &&
                 cJSON_IsString(id) && strlen(id->valuestring) == 32;
    if (valid) {
        for (size_t i = 0; i < 32; ++i)
            if (!((id->valuestring[i] >= '0' && id->valuestring[i] <= '9') ||
                  (id->valuestring[i] >= 'a' && id->valuestring[i] <= 'f'))) valid = false;
    }
    if (valid) memcpy(bridge_id, id->valuestring, 33);
    cJSON_Delete(decoded);
    return valid;
}

bool web_sync_ack(const char *url, const char *key, const char *firmware,
                  const char *boot_id) {
    select_locale_peer(url, key);
    cJSON *root = make_payload(firmware, boot_id, nullptr, nullptr, nullptr);
    char *payload = print_payload(root);
    if (!payload) { set_diagnostic("Sync:payload unavailable"); return false; }
    auto response = allocate_response();
    if (!response) { free(payload); return false; }
    bool ok = post_payload(url, key, payload, response.get(), nullptr);
    free(payload);
    return ok;
}

void web_sync_set_error(const char *error) {
    if (!error) { s_sync_error[0] = 0; return; }
    size_t n = strlen(error);
    if (n >= sizeof(s_sync_error)) return;
    for (size_t i = 0; i < n; ++i)
        if (!((error[i] >= 'a' && error[i] <= 'z') || error[i] == '_' ||
              (error[i] >= '0' && error[i] <= '9'))) return;
    memcpy(s_sync_error, error, n + 1);
}

bool web_sync_send_command_result(const char *url, const char *key,
                                  const char *firmware, const char *boot_id,
                                  const char *id, const char *state,
                                  const char *error) {
    if (!id || !state) return false;
    select_locale_peer(url, key);
    cJSON *root = make_payload(firmware, boot_id, id, state, error);
    char *payload = print_payload(root);
    if (!payload) { set_diagnostic("Sync:payload unavailable"); return false; }
    auto response = allocate_response();
    if (!response) { free(payload); return false; }
    bool ok = post_payload(url, key, payload, response.get(), nullptr);
    free(payload);
    return ok;
}

// Response bodies, credentials and network identifiers never reach the UI.
bool web_sync_copy_diagnostic(char *out, size_t size) {
    if (!out || !size) return false;
    auto mutex = s_diagnostic_mutex.load(std::memory_order_acquire);
    if (!mutex) { snprintf(out, size, "Sync:waiting"); return false; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    snprintf(out, size, "%s", s_diagnostic);
    xSemaphoreGive(mutex);
    return true;
}
