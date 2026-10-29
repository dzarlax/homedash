#include "web_settings.h"
#include "web_settings_store.h"
#include "nvs.h"
#include "recovery_security.h"
#include "web_identity.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

using bytes = std::vector<unsigned char>;
static std::map<std::string, std::map<std::string, bytes>> db;
static std::map<int, std::string> handles;
static std::map<int, std::map<std::string, bytes>> staged;
static int next_handle = 1;
static std::string fail_commit_namespace;
static int fail_commit_count = 0;
struct mock_http { std::map<std::string, std::string> headers; };
static unsigned random_seed = 1;

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key,
                                     const char *value) {
    if (!client || !key || !value) return ESP_FAIL;
    client->headers[key] = value;
    return ESP_OK;
}
void esp_fill_random(void *buffer, size_t length) {
    auto *bytes = static_cast<unsigned char *>(buffer);
    for (size_t i = 0; i < length; ++i) bytes[i] = static_cast<unsigned char>(random_seed++);
}

esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out) {
    if (mode == NVS_READONLY && !db.count(name)) return ESP_ERR_NVS_NOT_FOUND;
    int h = next_handle++;
    handles[h] = name;
    staged[h] = {};
    *out = h;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { handles.erase(h); staged.erase(h); }
static const bytes *get_value(nvs_handle_t h, const char *key) {
    auto ns = db.find(handles[h]);
    if (ns == db.end()) return nullptr;
    auto item = ns->second.find(key);
    return item == ns->second.end() ? nullptr : &item->second;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *length) {
    const bytes *value = get_value(h, key);
    if (!value) return ESP_ERR_NVS_NOT_FOUND;
    if (!length) return ESP_ERR_NVS_INVALID_LENGTH;
    if (!out) { *length = value->size(); return ESP_OK; }
    if (*length < value->size()) { *length = value->size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out, value->data(), value->size()); *length = value->size(); return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length) {
    const bytes *value = get_value(h, key);
    if (!value) return ESP_ERR_NVS_NOT_FOUND;
    if (!length) return ESP_ERR_NVS_INVALID_LENGTH;
    if (!out) { *length = value->size(); return ESP_OK; }
    if (*length < value->size()) { *length = value->size(); return ESP_ERR_NVS_INVALID_LENGTH; }
    memcpy(out, value->data(), value->size()); *length = value->size(); return ESP_OK;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value) {
    bytes b(value, value + strlen(value) + 1); staged[h][key] = b; return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *value, size_t size) {
    const auto *p = static_cast<const unsigned char *>(value);
    staged[h][key] = bytes(p, p + size); return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key) {
    staged[h][key] = {}; staged[h][key].push_back(0xff); return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) {
    auto &space = db[handles[h]];
    for (const auto &entry : staged[h]) {
        if (entry.second.size() == 1 && entry.second[0] == 0xff) space.erase(entry.first);
        else space[entry.first] = entry.second;
    }
    if (handles[h] == fail_commit_namespace && fail_commit_count > 0) {
        --fail_commit_count;
        return ESP_ERR_NVS_INVALID_LENGTH; // Simulate an ambiguous commit that wrote data.
    }
    return ESP_OK;
}

static web_preferences_t preferences(uint32_t revision, const char *timezone = "Europe/Belgrade",
                                     const char *posix = "CET-1CEST,M3.5.0,M10.5.0/3") {
    char json[1024];
    snprintf(json, sizeof(json),
        "{\"screen\":{\"name\":\"Hall\",\"city\":\"Belgrade\",\"timezone\":\"%s\","
        "\"clock_format\":\"12h\",\"idle_action\":\"off\",\"night_enabled\":true,"
        "\"night_start\":\"23:00\",\"night_end\":\"07:00\",\"auto_update_check\":true,"
        "\"auto_install\":false},\"photos\":{\"enabled\":true,\"idle_seconds\":300,"
        "\"interval_seconds\":60},\"visible\":{\"weather\":true,\"calendar\":true,"
        "\"transport\":true,\"health\":false,\"tasks\":true,\"news\":true,\"home\":true}}",
        timezone);
    web_preferences_t out = {};
    assert(web_preferences_parse(json, strlen(json), revision,
                                 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", posix,
                                 "https://bridge.example/settings/", &out));
    return out;
}

int main() {
    assert(web_identity_init());
    char stable_device_id[33] = {};
    assert(web_identity_get(stable_device_id, sizeof(stable_device_id)));
    assert(strlen(stable_device_id) == 32);
    for (const char *c = stable_device_id; *c; ++c)
        assert((*c >= '0' && *c <= '9') || (*c >= 'a' && *c <= 'f'));
    mock_http identity_client;
    assert(web_identity_set_http_header(&identity_client) == ESP_OK);
    assert(identity_client.headers["X-Device-ID"] == stable_device_id);
    assert(web_identity_matches(stable_device_id));
    assert(!web_identity_matches("ffffffffffffffffffffffffffffffff"));
    web_preferences_t revision_scope = preferences(8);
    assert(web_preferences_revision_allowed(&revision_scope, true, 7,
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
    assert(!web_preferences_revision_allowed(&revision_scope, true, 7,
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    const char *token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    assert(recovery_request_authorized("192.168.4.1", "192.168.4.2", "192.168.4.1",
        nullptr, nullptr, nullptr, token, false));
    assert(recovery_request_authorized("192.168.4.1", "192.168.4.2", "192.168.4.1",
        "http://192.168.4.1", "application/json", token, token, true));
    assert(!recovery_request_authorized("192.168.1.20", "192.168.1.5", "192.168.1.20",
        "http://192.168.1.20", "application/json", token, token, true));
    assert(!recovery_request_authorized("192.168.4.1", "192.168.4.2", "192.168.4.1",
        "http://attacker.example", "application/json", token, token, true));
    assert(!recovery_request_authorized("192.168.4.1", "192.168.4.2", "192.168.4.1",
        "http://192.168.4.1", "text/plain", token, token, true));
    assert(!recovery_request_authorized("192.168.4.1", "192.168.4.2", "192.168.4.1",
        "http://192.168.4.1", "application/json", "wrong", token, true));
    assert(!recovery_request_authorized("192.168.4.1", "10.0.0.2", "192.168.4.1",
        nullptr, nullptr, nullptr, token, false));

    web_preferences_t p = preferences(7);
    assert(p.revision == 7 && p.clock_12h && !p.idle_photos && !p.visible_health);
    assert(!web_preferences_valid(nullptr));
    web_preferences_t bad = p;
    strcpy(bad.posix_timezone, "UTC0");
    assert(!web_preferences_valid(&bad));
    bad = p; strcpy(bad.night_start, "25:60");
    assert(!web_preferences_valid(&bad));
    bad = p; bad.photo_interval_seconds = 14;
    assert(!web_preferences_valid(&bad));
    bad = p; memset(bad.name, 'x', sizeof(bad.name));
    assert(!web_preferences_valid(&bad));
    assert(web_command_id_valid("cmd_01-abc"));
    assert(!web_command_id_valid("bad id"));
    assert(web_command_is_new("older", "new-id"));
    assert(!web_command_is_new("same", "same"));

    device_config_t old_config = {};
    strcpy(old_config.wifi_ssid, "old-network");
    strcpy(old_config.wifi_pass, "old-password");
    strcpy(old_config.bridge_url, "https://old-bridge.example");
    strcpy(old_config.bridge_key, "old-key");
    assert(nvs_config_set_credentials(&old_config));
    device_config_t loaded = {};
    assert(nvs_config_load(&loaded) && !strcmp(loaded.bridge_key, "old-key"));
    web_preferences_t old_preferences = preferences(10);
    assert(web_settings_store_save(&old_preferences));

    // Ambiguous prefs save plus failed rollback must block all sync acknowledgements.
    assert(web_settings_store_network_trial_begin(&old_config, true, &old_preferences, true));
    assert(nvs_config_set_credentials(&old_config));
    web_preferences_t ambiguous_preferences = preferences(12);
    fail_commit_namespace = "webprefs";
    fail_commit_count = 1;
    assert(!web_settings_store_save(&ambiguous_preferences));
    assert(!web_settings_store_sync_allowed());
    fail_commit_count = 1;
    assert(!web_settings_store_network_trial_recover_pending());
    assert(!web_settings_store_sync_allowed());
    assert(web_settings_store_network_trial_recover_pending());
    assert(web_settings_store_sync_allowed());
    fail_commit_namespace.clear();

    // Power interruption after candidate config persistence but before prefs/revision.
    assert(web_settings_store_network_trial_begin(&old_config, true, &old_preferences, true));
    assert(!web_settings_store_sync_allowed());
    device_config_t candidate = old_config;
    strcpy(candidate.wifi_ssid, "new-network");
    strcpy(candidate.wifi_pass, "new-password");
    strcpy(candidate.bridge_url, "https://new-bridge.example");
    strcpy(candidate.bridge_key, "new-key");
    assert(nvs_config_set_credentials(&candidate));
    assert(web_settings_store_network_trial_recover_pending());
    assert(nvs_config_load(&loaded));
    assert(!strcmp(loaded.wifi_ssid, "old-network") && !strcmp(loaded.bridge_key, "old-key"));
    web_preferences_t loaded_prefs = {};
    assert(web_settings_store_load(&loaded_prefs) && loaded_prefs.revision == 10);

    // Power interruption after both values were written but before the committed marker.
    assert(web_settings_store_network_trial_begin(&old_config, true, &old_preferences, true));
    assert(nvs_config_set_credentials(&candidate));
    web_preferences_t interrupted_preferences = preferences(11);
    assert(web_settings_store_save(&interrupted_preferences));
    assert(web_settings_store_network_trial_recover_pending());
    assert(nvs_config_load(&loaded) && !strcmp(loaded.wifi_ssid, "old-network"));
    assert(web_settings_store_load(&loaded_prefs) && loaded_prefs.revision == 10);

    // A fully persisted/validated trial survives reboot and can then ACK the old Bridge.
    assert(web_settings_store_network_trial_begin(&old_config, true, &old_preferences, true));
    assert(nvs_config_set_credentials(&candidate));
    web_preferences_t new_preferences = preferences(11);
    assert(web_settings_store_save(&new_preferences));
    assert(web_settings_store_network_trial_mark_committed());
    assert(web_settings_store_sync_allowed());
    device_config_t journal_old = {};
    web_preferences_t journal_prefs = {};
    bool journal_had_config = false, journal_had_prefs = false, journal_committed = false;
    assert(web_settings_store_network_trial_read(&journal_old, &journal_had_config,
        &journal_prefs, &journal_had_prefs, &journal_committed));
    assert(journal_committed);
    assert(web_settings_store_network_trial_recover_pending());
    assert(nvs_config_load(&loaded) && !strcmp(loaded.bridge_key, "new-key"));
    assert(web_settings_store_load(&loaded_prefs) && loaded_prefs.revision == 11);
    assert(web_settings_store_network_trial_clear());
    char after_handoff_id[33] = {};
    assert(web_identity_init() && web_identity_get(after_handoff_id, sizeof(after_handoff_id)));
    assert(!strcmp(stable_device_id, after_handoff_id));

    assert(web_settings_store_command_begin("restart-1", "restart", "boot-a", "1.0.21"));
    assert(!web_settings_store_command_begin("restart-1", "restart", "boot-a", "1.0.21"));
    assert(web_settings_store_recover_on_boot("boot-b", "1.0.21", true));
    char id[65] = {}, state[16] = {}, error[96] = {};
    assert(web_settings_store_command_result(id, sizeof(id), state, sizeof(state), error, sizeof(error)));
    assert(!strcmp(id, "restart-1") && !strcmp(state, "succeeded"));
    assert(web_settings_store_command_begin("ota-1", "ota_install", "boot-b", "1.0.21"));
    assert(web_settings_store_recover_on_boot("boot-c", "1.0.21", true));
    assert(web_settings_store_command_result(id, sizeof(id), state, sizeof(state), error, sizeof(error)));
    assert(!strcmp(state, "failed") && !strcmp(error, "ota_version_not_changed"));
    assert(web_settings_store_command_begin("reset-1", "reset", "boot-c", "1.0.21"));
    assert(!web_settings_store_recover_on_boot("boot-d", "1.0.21", false));
    assert(web_settings_store_complete_reset_after_recovery());
    assert(web_settings_store_command_result(id, sizeof(id), state, sizeof(state), error, sizeof(error)));
    assert(!strcmp(state, "succeeded"));

    nvs_handle_t unrelated = 0;
    assert(nvs_open("system", NVS_READWRITE, &unrelated) == ESP_OK);
    assert(nvs_set_str(unrelated, "owner", "keep") == ESP_OK);
    assert(nvs_commit(unrelated) == ESP_OK);
    nvs_close(unrelated);
    assert(nvs_config_reset_device());
    assert(web_settings_store_clear_preferences());
    char after_reset_id[33] = {};
    assert(web_identity_init() && web_identity_get(after_reset_id, sizeof(after_reset_id)));
    assert(!strcmp(stable_device_id, after_reset_id));
    assert(nvs_open("system", NVS_READONLY, &unrelated) == ESP_OK);
    char owner[8] = {};
    size_t owner_size = sizeof(owner);
    assert(nvs_get_str(unrelated, "owner", owner, &owner_size) == ESP_OK);
    assert(!strcmp(owner, "keep"));
    nvs_close(unrelated);
    puts("PASS: settings validation, NVS rollback/commit, command replay and post-boot results");
}
