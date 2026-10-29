#include "web_settings_store.h"

#include <string.h>
#include "nvs.h"
#include "esp_log.h"

namespace {
constexpr uint32_t kStoreVersion = 2;
struct stored_preferences_t {
    uint32_t version;
    web_preferences_t value;
};
struct network_trial_t {
    uint32_t version;
    bool committed;
    bool had_config;
    bool had_preferences;
    device_config_t old_config;
    stored_preferences_t old_preferences;
};
constexpr uint32_t kNetworkTrialVersion = 1;
static const char *TAG = "web_settings";

bool read_string(nvs_handle_t h, const char *key, char *out, size_t size) {
    size_t n = size;
    esp_err_t err = nvs_get_str(h, key, out, &n);
    return err == ESP_OK && n > 0 && n <= size;
}
}

bool web_settings_store_load(web_preferences_t *out) {
    if (!out) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READONLY, &h) != ESP_OK) return false;
    stored_preferences_t stored = {};
    size_t n = sizeof(stored);
    esp_err_t err = nvs_get_blob(h, "prefs", &stored, &n);
    nvs_close(h);
    if (err != ESP_OK || n != sizeof(stored) || stored.version != kStoreVersion ||
        !web_preferences_valid(&stored.value)) return false;
    *out = stored.value;
    return true;
}

bool web_settings_store_save(const web_preferences_t *prefs) {
    if (!web_preferences_valid(prefs)) return false;
    stored_preferences_t stored = {kStoreVersion, *prefs};
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, "prefs", &stored, sizeof(stored));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "Could not persist preferences (%s)", esp_err_to_name(err));
    return err == ESP_OK;
}

bool web_settings_store_command_begin(const char *id, const char *action,
                                     const char *boot_id, const char *firmware) {
    if (!web_command_id_valid(id) || !action || strlen(action) >= 24 ||
        !boot_id || strlen(boot_id) >= 24 || !firmware || strlen(firmware) >= 32) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    char last[65] = {};
    bool read = read_string(h, "last_cmd", last, sizeof(last));
    if (read && !web_command_is_new(last, id)) { nvs_close(h); return false; }
    esp_err_t err = nvs_set_str(h, "last_cmd", id);
    if (err == ESP_OK) err = nvs_set_str(h, "cmd_state", "accepted");
    if (err == ESP_OK) err = nvs_set_str(h, "cmd_action", action);
    if (err == ESP_OK) err = nvs_set_str(h, "cmd_boot", boot_id);
    if (err == ESP_OK) err = nvs_set_str(h, "cmd_fw", firmware);
    if (err == ESP_OK) err = nvs_erase_key(h, "cmd_error");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool web_settings_store_command_finish(const char *id, const char *state,
                                       const char *error) {
    if (!web_command_id_valid(id) || !state ||
        (strcmp(state, "succeeded") && strcmp(state, "failed"))) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    char last[65] = {};
    bool same = read_string(h, "last_cmd", last, sizeof(last)) && !strcmp(last, id);
    esp_err_t err = same ? nvs_set_str(h, "cmd_state", state) : ESP_ERR_NVS_NOT_FOUND;
    if (err == ESP_OK) {
        if (error && error[0]) err = nvs_set_str(h, "cmd_error", error);
        else {
            err = nvs_erase_key(h, "cmd_error");
            if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
        }
    }
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool web_settings_store_command_result(char *id, size_t id_size,
                                       char *state, size_t state_size,
                                       char *error, size_t error_size) {
    if (!id || !id_size || !state || !state_size || !error || !error_size) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = read_string(h, "last_cmd", id, id_size) &&
              read_string(h, "cmd_state", state, state_size);
    if (ok && !read_string(h, "cmd_error", error, error_size)) error[0] = 0;
    nvs_close(h);
    return ok;
}

bool web_settings_store_command_id(char *id, size_t size) {
    if (!id || !size) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = read_string(h, "last_cmd", id, size);
    nvs_close(h);
    return ok;
}

bool web_settings_store_clear_preferences(void) {
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(h, "prefs");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool web_settings_store_recover_on_boot(const char *boot_id, const char *firmware,
                                        bool config_present) {
    if (!boot_id || !firmware) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READONLY, &h) != ESP_OK) return false;
    char id[65] = {}, state[16] = {}, action[24] = {}, original_boot[24] = {}, original_fw[32] = {};
    bool accepted = read_string(h, "last_cmd", id, sizeof(id)) &&
                    read_string(h, "cmd_state", state, sizeof(state)) &&
                    read_string(h, "cmd_action", action, sizeof(action)) &&
                    read_string(h, "cmd_boot", original_boot, sizeof(original_boot)) &&
                    read_string(h, "cmd_fw", original_fw, sizeof(original_fw)) &&
                    !strcmp(state, "accepted") && strcmp(original_boot, boot_id);
    nvs_close(h);
    if (!accepted) return false;
    if (!strcmp(action, "reset") && !config_present) return false;
    if (!strcmp(action, "restart"))
        return web_settings_store_command_finish(id, "succeeded", nullptr);
    if (!strcmp(action, "ota_install")) {
        if (strcmp(original_fw, firmware))
            return web_settings_store_command_finish(id, "succeeded", nullptr);
        return web_settings_store_command_finish(id, "failed", "ota_version_not_changed");
    }
    if (!strcmp(action, "reset"))
        return web_settings_store_command_finish(id, "failed", "reset_not_completed");
    return web_settings_store_command_finish(id, "failed", "interrupted_before_result");
}

bool web_settings_store_complete_reset_after_recovery(void) {
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READONLY, &h) != ESP_OK) return false;
    char id[65] = {}, state[16] = {}, action[24] = {};
    bool is_reset = read_string(h, "last_cmd", id, sizeof(id)) &&
                    read_string(h, "cmd_state", state, sizeof(state)) &&
                    read_string(h, "cmd_action", action, sizeof(action)) &&
                    !strcmp(state, "accepted") && !strcmp(action, "reset");
    nvs_close(h);
    return !is_reset || web_settings_store_command_finish(id, "succeeded", nullptr);
}

bool web_settings_store_network_trial_begin(const device_config_t *old_config,
                                            bool had_config,
                                            const web_preferences_t *old_preferences,
                                            bool had_preferences) {
    if (!old_config || (had_preferences && (!old_preferences ||
        !web_preferences_valid(old_preferences)))) return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    network_trial_t existing = {};
    size_t n = sizeof(existing);
    esp_err_t get_err = nvs_get_blob(h, "net_trial", &existing, &n);
    if (get_err == ESP_OK) { nvs_close(h); return false; }
    if (get_err != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return false; }
    network_trial_t trial = {};
    trial.version = kNetworkTrialVersion;
    trial.had_config = had_config;
    trial.had_preferences = had_preferences;
    trial.old_config = *old_config;
    if (had_preferences) {
        trial.old_preferences.version = kStoreVersion;
        trial.old_preferences.value = *old_preferences;
    }
    esp_err_t err = nvs_set_blob(h, "net_trial", &trial, sizeof(trial));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool web_settings_store_network_trial_read(device_config_t *old_config,
                                           bool *had_config,
                                           web_preferences_t *old_preferences,
                                           bool *had_preferences,
                                           bool *committed) {
    if (!old_config || !had_config || !old_preferences || !had_preferences || !committed)
        return false;
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READONLY, &h) != ESP_OK) return false;
    network_trial_t trial = {};
    size_t n = sizeof(trial);
    esp_err_t err = nvs_get_blob(h, "net_trial", &trial, &n);
    nvs_close(h);
    if (err != ESP_OK || n != sizeof(trial) || trial.version != kNetworkTrialVersion) return false;
    *old_config = trial.old_config;
    *had_config = trial.had_config;
    *had_preferences = trial.had_preferences;
    *committed = trial.committed;
    memset(old_preferences, 0, sizeof(*old_preferences));
    if (trial.had_preferences) {
        if (trial.old_preferences.version != kStoreVersion ||
            !web_preferences_valid(&trial.old_preferences.value)) return false;
        *old_preferences = trial.old_preferences.value;
    }
    return true;
}

bool web_settings_store_network_trial_mark_committed(void) {
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    network_trial_t trial = {};
    size_t n = sizeof(trial);
    esp_err_t err = nvs_get_blob(h, "net_trial", &trial, &n);
    if (err == ESP_OK && n == sizeof(trial) && trial.version == kNetworkTrialVersion) {
        trial.committed = true;
        err = nvs_set_blob(h, "net_trial", &trial, sizeof(trial));
        if (err == ESP_OK) err = nvs_commit(h);
    } else if (err == ESP_OK) {
        err = ESP_ERR_NVS_INVALID_LENGTH;
    }
    nvs_close(h);
    return err == ESP_OK;
}

bool web_settings_store_network_trial_clear(void) {
    nvs_handle_t h;
    if (nvs_open("webprefs", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_erase_key(h, "net_trial");
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

bool web_settings_store_network_trial_recover_pending(void) {
    device_config_t old_config = {};
    web_preferences_t old_preferences = {};
    bool had_config = false, had_preferences = false, committed = false;
    if (!web_settings_store_network_trial_read(&old_config, &had_config, &old_preferences,
                                                &had_preferences, &committed)) {
        // A missing journal is normal; distinguish it from a corrupt existing record.
        nvs_handle_t h;
        esp_err_t open_err = nvs_open("webprefs", NVS_READONLY, &h);
        if (open_err == ESP_ERR_NVS_NOT_FOUND) return true;
        if (open_err != ESP_OK) return false;
        size_t n = 0;
        esp_err_t err = nvs_get_blob(h, "net_trial", nullptr, &n);
        nvs_close(h);
        return err == ESP_ERR_NVS_NOT_FOUND;
    }
    if (committed) return true;
    bool config_ok = had_config ? nvs_config_set_credentials(&old_config)
                                : nvs_config_reset_device();
    if (!config_ok) return false;
    bool prefs_ok = had_preferences ? web_settings_store_save(&old_preferences)
                                    : web_settings_store_clear_preferences();
    if (!prefs_ok) return false;
    return web_settings_store_network_trial_clear();
}

bool web_settings_store_sync_allowed(void) {
    nvs_handle_t h;
    esp_err_t open_err = nvs_open("webprefs", NVS_READONLY, &h);
    if (open_err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (open_err != ESP_OK) return false;
    network_trial_t trial = {};
    size_t n = sizeof(trial);
    esp_err_t err = nvs_get_blob(h, "net_trial", &trial, &n);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK || n != sizeof(trial) || trial.version != kNetworkTrialVersion)
        return false;
    return trial.committed;
}
