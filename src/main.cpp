#include "brightness_runtime.h"
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

#include "lvgl_port.h"
#include "gt911.h"
#include "config.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "bridge.h"
#include "ota.h"
#include "ui_dashboard.h"
#include "photo_frame.h"
#include "web_settings.h"
#include "web_settings_store.h"
#include "web_sync.h"
#include "web_identity.h"
#include "recovery_ap.h"
#include "esp_timer.h"
#include "action_feedback_runtime.h"
#include "action_executor.h"

static const char *TAG = "main";

// Network task handle for cross-core notification
struct recovery_job_t { web_network_update_t update; SemaphoreHandle_t done; bool result; };
enum request_kind_t { REQUEST_CALENDAR, REQUEST_TOGGLE, REQUEST_CLIMATE, REQUEST_OTA, REQUEST_RECOVERY, REQUEST_RECOVERY_STOP, REQUEST_RECOVERY_CONFIG };
struct network_request_t { request_kind_t kind; int year, month, day; char entity[48]; char action[20]; char mode[16]; float temperature; recovery_job_t *recovery; uint32_t action_id; };
static QueueHandle_t s_request_queue = NULL;
static device_config_t s_runtime_config = {};
static char s_boot_id[17] = {};
static bool s_network_handoff_pending = false;
static volatile bool s_settings_fail_closed = false;
static device_config_t s_pending_old_config = {};
static device_config_t s_pending_new_config = {};

void request_recovery_ap(void)
{
    network_request_t req = { REQUEST_RECOVERY, 0, 0, 0, {} };
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

void request_stop_recovery_ap(void)
{
    network_request_t req = { REQUEST_RECOVERY_STOP, 0, 0, 0, {} };
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

static void run_web_command(const web_command_t *command, const device_config_t *cfg,
                            const char *boot_id)
{
    if (!command || !command->id[0] || !web_command_is_new(nullptr, command->id)) return;
    char last[65] = {};
    if (web_settings_store_command_id(last, sizeof(last)) &&
        !web_command_is_new(last, command->id)) return;
    if (!web_settings_store_command_begin(command->id, command->action, boot_id, FW_VERSION)) return;
    web_sync_send_command_result(cfg->bridge_url, cfg->bridge_key, FW_VERSION,
                                 boot_id, command->id, "accepted", nullptr);

    bool succeeded = false;
    bool restart_now = false;
    const char *error = nullptr;
    if (!strcmp(command->action, "photo_start")) {
        if (lvgl_port_lock(-1)) {
            succeeded = photo_frame_start_now();
            lvgl_port_unlock();
        }
        if (!succeeded) error = "photo_start_failed";
    } else if (!strcmp(command->action, "restart")) {
        restart_now = true;
    } else if (!strcmp(command->action, "reset")) {
        web_preferences_t old_preferences = {};
        bool had_preferences = web_settings_store_load(&old_preferences);
        device_config_t old_config = *cfg;
        bool cleared_preferences = web_settings_store_clear_preferences();
        bool cleared_config = cleared_preferences && nvs_config_reset_device();
        if (!cleared_config || !recovery_ap_start()) {
            nvs_config_set_credentials(&old_config);
            if (had_preferences) web_settings_store_save(&old_preferences);
            error = cleared_config ? "reset_recovery_failed" : "reset_persistence_failed";
        } else {
            succeeded = true;
        }
    } else if (!strcmp(command->action, "backlight_preview") || !strcmp(command->action, "backlight_restore")) {
        // Runtime serializes with the restore worker; recovery must not wait
        // for LVGL, nor be rejected solely because a prior write failed.
        bool ready = brightness_runtime_ready();
        succeeded = !strcmp(command->action, "backlight_preview") ?
            brightness_runtime_preview(command->percent) : brightness_runtime_restore();
        if (!succeeded) error = ready || brightness_runtime_error()[0] ? "device_error" : "unsupported_action";
    } else if (!strcmp(command->action, "ota_check")) {
        bool available = false;
        succeeded = ota_check_available(&available);
        if (!succeeded) error = "ota_check_failed";
    } else if (!strcmp(command->action, "ota_install")) {
        bool available = false;
        if (!ota_check_available(&available)) error = "ota_check_failed";
        else if (!available) error = "no_update";
        else if (command->target_version[0] && strcmp(command->target_version, ota_available_version())) error = "device_error";
        else {
            char target[32] = {};
            snprintf(target, sizeof(target), "%s", ota_available_version());
            if (!ota_install_available()) error = "device_error";
            else {
                // Success is confirmed after reboot only when this durable marker matches.
                if (!web_settings_store_ota_written(command->id, target))
                    web_sync_set_error("command_result_persistence_failed");
                restart_now = true;
            }
        }
    } else {
        // Brightness has no trustworthy calibrated baseline on this device.
        error = "unsupported_action";
    }

    if (restart_now) {
        esp_restart();
        return;
    }
    const char *final_state = succeeded ? "succeeded" : "failed";
    bool result_persisted = web_settings_store_command_finish(command->id, final_state, error);
    if (!result_persisted) {
        char id[65] = {}, state[16] = {}, stored_error[96] = {};
        result_persisted = web_settings_store_command_result(
            id, sizeof(id), state, sizeof(state), stored_error, sizeof(stored_error)) &&
            !strcmp(id, command->id) && !strcmp(state, final_state) &&
            ((!error || !error[0]) ? !stored_error[0] : !strcmp(error, stored_error));
    }
    if (result_persisted) {
        web_sync_send_command_result(cfg->bridge_url, cfg->bridge_key, FW_VERSION,
                                     boot_id, command->id, final_state, error);
    } else {
        web_sync_set_error("command_result_persistence_failed");
    }
    if (succeeded && !strcmp(command->action, "reset")) {
        wifi_forget_station();
        memset(&s_runtime_config, 0, sizeof(s_runtime_config));
        wifi_set_timezone(POSIX_TZ);
        if (lvgl_port_lock(-1)) {
            ui_dashboard_reset_preferences();
            lvgl_port_unlock();
        }
        photo_frame_apply_web_preferences(true, 300, 60);
        bridge_init("", "");
        ota_init("", "");
        photo_frame_update_network("", "");
        return;
    }
}

static void ota_managed_poll(void)
{
    web_preferences_t prefs = {};
    bool managed = web_settings_store_load(&prefs);
    if (!managed) {
        ota_check_and_update();
        return;
    }
    if (!prefs.auto_update_check) return;
    bool available = false;
    if (ota_check_available(&available) && available && prefs.auto_install &&
        ota_install_available()) esp_restart();
}

static void ota_manual_check(void)
{
    web_preferences_t prefs = {};
    if (!web_settings_store_load(&prefs)) {
        ota_check_and_update();
        return;
    }
    bool available = false;
    ota_check_available(&available);
}

static void restore_runtime_preferences(void)
{
    web_preferences_t prefs = {};
    if (web_settings_store_load(&prefs)) {
        if (lvgl_port_lock(-1)) {
            ui_dashboard_apply_preferences(&prefs);
            lvgl_port_unlock();
        }
        photo_frame_apply_web_preferences(prefs.photos_enabled, prefs.photo_idle_seconds,
                                          prefs.photo_interval_seconds);
        wifi_set_timezone(prefs.posix_timezone);
        photo_frame_confirm_timezone(prefs.timezone, prefs.posix_timezone);
    } else {
        if (lvgl_port_lock(-1)) {
            ui_dashboard_reset_preferences();
            lvgl_port_unlock();
        }
        photo_frame_apply_web_preferences(true, 300, 60);
        wifi_set_timezone(POSIX_TZ);
    }
}

static void halt_if_settings_fail_closed(void)
{
    if (!s_settings_fail_closed) return;
    ESP_LOGE(TAG, "Settings transaction state is unresolved; network task halted");
    for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
}

static bool stage_network_update(const web_network_update_t *update,
                                 const device_config_t *cfg, const char *boot_id,
                                 device_config_t *candidate_out, bool *changed_out,
                                 bool *wifi_trial_out, char *target_bridge_id,
                                 size_t target_bridge_id_size)
{
    if (changed_out) *changed_out = false;
    if (wifi_trial_out) *wifi_trial_out = false;
    if (!update || !cfg || !candidate_out || !target_bridge_id ||
        target_bridge_id_size < 33) return false;
    target_bridge_id[0] = 0;
    const bool any = update->has_wifi_ssid || update->has_wifi_password ||
                     update->has_bridge_url || update->has_bridge_key;
    if (!any) { *candidate_out = *cfg; return true; }
    device_config_t candidate = *cfg;
    if (update->has_wifi_ssid) strcpy(candidate.wifi_ssid, update->wifi_ssid);
    if (update->has_wifi_password) strcpy(candidate.wifi_pass, update->wifi_password);
    if (update->has_bridge_url) strcpy(candidate.bridge_url, update->bridge_url);
    if (update->has_bridge_key) strcpy(candidate.bridge_key, update->bridge_key);
    if (!candidate.wifi_ssid[0] || !candidate.bridge_url[0] || !candidate.bridge_key[0]) {
        web_sync_set_error("network_trial_invalid");
        return false;
    }
    const bool wifi_change = strcmp(cfg->wifi_ssid, candidate.wifi_ssid) ||
                             strcmp(cfg->wifi_pass, candidate.wifi_pass);
    const bool changed = wifi_change || strcmp(cfg->bridge_url, candidate.bridge_url) ||
                         strcmp(cfg->bridge_key, candidate.bridge_key);
    *candidate_out = candidate;
    if (!changed) return true;
    web_preferences_t old_preferences = {};
    bool had_preferences = web_settings_store_load(&old_preferences);
    if (!web_settings_store_network_trial_begin(cfg, cfg->wifi_ssid[0] != 0,
                                                had_preferences ? &old_preferences : nullptr,
                                                had_preferences)) {
        web_sync_set_error("network_trial_journal_failed");
        return false;
    }
    bool trial_started = false;
    if (wifi_change) {
        trial_started = wifi_begin_trial(candidate.wifi_ssid, candidate.wifi_pass, 15000);
        if (!trial_started) {
            web_sync_set_error("network_trial_failed");
            if (!web_settings_store_network_trial_clear()) s_settings_fail_closed = true;
            return false;
        }
    }
    if (!web_sync_probe_bridge(candidate.bridge_url, candidate.bridge_key,
                               target_bridge_id, target_bridge_id_size)) {
        if (trial_started) wifi_rollback_trial();
        web_sync_set_error("network_bridge_validation_failed");
        if (!web_settings_store_network_trial_clear()) s_settings_fail_closed = true;
        return false;
    }
    if (!nvs_config_set_credentials(&candidate)) {
        if (trial_started) wifi_rollback_trial();
        web_sync_set_error("network_persistence_failed");
        if (!web_settings_store_network_trial_recover_pending()) {
            ESP_LOGE(TAG, "Network persistence failed and rollback is pending");
            s_settings_fail_closed = true;
        }
        return false;
    }
    if (changed_out) *changed_out = changed;
    if (wifi_trial_out) *wifi_trial_out = trial_started;
    web_sync_set_error(nullptr);
    return true;
}

static void finalize_network_handoff(const device_config_t *candidate, bool wifi_trial,
                                     bool clear_journal = true)
{
    if (!candidate) return;
    if (wifi_trial) wifi_confirm_trial();
    s_runtime_config = *candidate;
    bridge_init(candidate->bridge_url, candidate->bridge_key);
    ota_init(candidate->bridge_url, candidate->bridge_key);
    photo_frame_update_network(candidate->bridge_url, candidate->bridge_key);
    if (clear_journal && !web_settings_store_network_trial_clear())
        ESP_LOGE(TAG, "Committed network handoff journal could not be cleared");
    s_network_handoff_pending = false;
}

static bool apply_recovery_network_update_now(const web_network_update_t *update)
{
    device_config_t candidate = {};
    bool changed = false, trial = false;
    char target_bridge_id[33] = {};
    if (!stage_network_update(update, &s_runtime_config, s_boot_id, &candidate,
                              &changed, &trial, target_bridge_id,
                              sizeof(target_bridge_id))) return false;
    if (changed) {
        if (!web_settings_store_network_trial_mark_committed()) {
            device_config_t old_config = {};
            web_preferences_t old_preferences = {};
            bool had_config = false, had_preferences = false, committed = false;
            if (!web_settings_store_network_trial_read(&old_config, &had_config,
                    &old_preferences, &had_preferences, &committed)) {
                s_settings_fail_closed = true;
                return false;
            }
            if (!committed) {
                if (trial) wifi_rollback_trial();
                if (!web_settings_store_network_trial_recover_pending()) {
                    s_settings_fail_closed = true;
                    return false;
                }
                return false;
            }
        }
        finalize_network_handoff(&candidate, trial);
    }
    return true;
}

// The HTTP task waits for the network task instead of mutating live
// credentials, NVS journals and HTTP clients concurrently with polling.
bool recovery_apply_network_update(const web_network_update_t *update)
{
    if (!update || !s_request_queue) return false;
    recovery_job_t job = {};
    job.update = *update;
    job.done = xSemaphoreCreateBinary();
    if (!job.done) return false;
    network_request_t request = {};
    request.kind = REQUEST_RECOVERY_CONFIG;
    request.recovery = &job;
    if (xQueueSend(s_request_queue, &request, pdMS_TO_TICKS(1000)) != pdTRUE) {
        vSemaphoreDelete(job.done);
        return false;
    }
    // The caller owns the job until completion. A timeout must not release
    // its storage while the network task could still commit the change.
    xSemaphoreTake(job.done, portMAX_DELAY);
    vSemaphoreDelete(job.done);
    return job.result;
}

static void complete_recovery_request(const network_request_t *request)
{
    recovery_job_t *job = request->recovery;
    if (!job) return;
    job->result = apply_recovery_network_update_now(&job->update);
    xSemaphoreGive(job->done);
}

void request_calendar_date(int year, int month, int day)
{
    network_request_t req = { REQUEST_CALENDAR, year, month, day, {} };
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

// The tracker is touched only while holding the LVGL lock, including by the network task.
static ActionFeedback s_action_feedback;
static uint64_t action_now_ms() { return (uint64_t)(esp_timer_get_time() / 1000); }
const action_record_t *home_action_find(const char *entity) { return s_action_feedback.find(entity); }
void home_action_tick(void) { s_action_feedback.tick(action_now_ms()); }
uint32_t request_home_action(const char *entity, const char *action, float target, const char *mode)
{
    uint32_t id = s_action_feedback.begin(entity, action, target, mode, action_now_ms());
    if (!id) return 0;
    if (!wifi_is_connected()) { s_action_feedback.result(id, ACTION_NOT_SENT, action_now_ms(), ACTION_ERROR_OFFLINE); return id; }
    network_request_t req = {};
    req.kind = REQUEST_CLIMATE; req.action_id = id; req.temperature = target;
    strncpy(req.entity, entity, sizeof(req.entity)-1);
    strncpy(req.action, action, sizeof(req.action)-1);
    if (mode) strncpy(req.mode, mode, sizeof(req.mode)-1);
    if (!s_request_queue || xQueueSend(s_request_queue, &req, 0) != pdTRUE)
        s_action_feedback.result(id, ACTION_NOT_SENT, action_now_ms(), ACTION_ERROR_QUEUE_FULL);
    return id;
}

static uint64_t executor_now(void *) { return action_now_ms(); }
static bool executor_dispatch(void *, uint32_t id)
{
    bool dispatch = false;
    if (lvgl_port_lock(-1)) { dispatch = s_action_feedback.dispatch(id, action_now_ms()); lvgl_port_unlock(); }
    return dispatch;
}
static bridge_action_result_t executor_post(void *, const action_request_t &req)
{ return bridge_action_request(req.entity, req.action, req.target, req.mode); }
static bridge_action_result_t executor_read(void *, const char *entity, int timeout)
{ return bridge_action_read(entity, timeout); }
static void executor_publish_read(void *, uint32_t id, const bridge_action_result_t &result)
{
    if (!result.has_observation) return;
    if (lvgl_port_lock(-1)) {
        if (s_action_feedback.by_id(id)) {
            bridge_action_merge_observation(result.observation);
            s_action_feedback.observe(id, result.observation, action_now_ms());
            ui_dashboard_refresh_ha();
        }
        lvgl_port_unlock();
    }
}
static void executor_publish_post(void *, uint32_t id, const bridge_action_result_t &result)
{
    if (lvgl_port_lock(-1)) {
        s_action_feedback.result(id, result.phase, action_now_ms(), result.error);
        if (result.has_observation && s_action_feedback.by_id(id)) {
            bridge_action_merge_observation(result.observation);
            s_action_feedback.observe(id, result.observation, action_now_ms());
            ui_dashboard_refresh_ha();
        }
        lvgl_port_unlock();
    }
}
static bool executor_waiting(void *, uint32_t id, uint64_t *deadline)
{
    bool waiting = false;
    if (lvgl_port_lock(-1)) {
        const action_record_t *r = s_action_feedback.by_id(id);
        if (r && !r->timed_out && (r->phase == ACTION_ACCEPTED || r->phase == ACTION_UNKNOWN)) { waiting = true; *deadline = r->deadline_ms; }
        lvgl_port_unlock();
    }
    return waiting;
}
static ActionExecutor s_action_executor({nullptr, executor_now, executor_dispatch, executor_post,
    executor_read, executor_publish_post, executor_publish_read, executor_waiting});
static void action_run_request(const network_request_t &req)
{
    action_request_t action = {}; action.id = req.action_id; action.target = req.temperature;
    strncpy(action.entity, req.entity, sizeof(action.entity)-1);
    strncpy(action.action, req.action, sizeof(action.action)-1);
    strncpy(action.mode, req.mode, sizeof(action.mode)-1);
    s_action_executor.run(action);
}

// Shared light toggle request (written by UI core, read by network core)
void request_light_toggle(const char *entity_id)
{
    network_request_t req = { REQUEST_TOGGLE, 0, 0, 0, {} };
    if (entity_id) strncpy(req.entity, entity_id, sizeof(req.entity) - 1);
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

void request_climate_action(const char *entity_id, const char *action, float temperature, const char *mode)
{
    network_request_t req = { REQUEST_CLIMATE, 0, 0, 0, {}, {}, {}, temperature };
    if (entity_id) strncpy(req.entity, entity_id, sizeof(req.entity) - 1);
    if (action) strncpy(req.action, action, sizeof(req.action) - 1);
    if (mode) strncpy(req.mode, mode, sizeof(req.mode) - 1);
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

// Manual OTA check request (from UI button)
void request_ota_check(void)
{
    network_request_t req = { REQUEST_OTA, 0, 0, 0, {} };
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

// Background task: WiFi + NTP + Bridge polling + OTA (runs on core 0)
static void network_task(void *param)
{
    // Wait for LVGL to render first frame
    vTaskDelay(pdMS_TO_TICKS(2000));
    snprintf(s_boot_id, sizeof(s_boot_id), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    if (!web_identity_init()) {
        ESP_LOGE(TAG, "Stable device identity is unavailable; native API disabled");
        for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
    }

    // Load config from NVS
    device_config_t &cfg = s_runtime_config;
    bool config_present = nvs_config_load(&cfg);
    if (!web_settings_store_network_trial_recover_pending()) {
        ESP_LOGE(TAG, "Interrupted network change could not be rolled back safely");
        for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
    }
    config_present = nvs_config_load(&cfg);
    web_settings_store_recover_on_boot(s_boot_id, FW_VERSION, config_present);
    if (!config_present) {
        ESP_LOGW(TAG, "Device setup missing; starting protected local setup");
        if (recovery_ap_start()) {
            web_settings_store_complete_reset_after_recovery();
            if (lvgl_port_lock(-1)) {
                ui_dashboard_open_settings();
                lvgl_port_unlock();
            }
        }
        else ESP_LOGE(TAG, "Local setup could not start");
        for (;;) {
            network_request_t setup_req = {};
            if (xQueueReceive(s_request_queue, &setup_req, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (setup_req.kind == REQUEST_RECOVERY) recovery_ap_start();
                else if (setup_req.kind == REQUEST_RECOVERY_STOP) recovery_ap_stop();
                else if (setup_req.kind == REQUEST_RECOVERY_CONFIG) complete_recovery_request(&setup_req);
            }
        }
    }

    device_config_t old_trial_config = {};
    static web_preferences_t old_trial_preferences = {};
    bool trial_had_config = false, trial_had_preferences = false, trial_committed = false;
    if (web_settings_store_network_trial_read(&old_trial_config, &trial_had_config,
            &old_trial_preferences, &trial_had_preferences, &trial_committed) && trial_committed) {
        if (trial_had_config) {
            s_network_handoff_pending = true;
            s_pending_old_config = old_trial_config;
            s_pending_new_config = cfg;
        } else {
            web_settings_store_network_trial_clear();
        }
    }

    static web_preferences_t saved_prefs = {};
    bool has_saved_prefs = web_settings_store_load(&saved_prefs);
    if (has_saved_prefs) {
        if (lvgl_port_lock(-1)) {
            ui_dashboard_apply_preferences(&saved_prefs);
            lvgl_port_unlock();
        }
        photo_frame_apply_web_preferences(saved_prefs.photos_enabled,
                                          saved_prefs.photo_idle_seconds,
                                          saved_prefs.photo_interval_seconds);
    }

    // Initialize bridge with runtime URL
    const device_config_t *active_config = s_network_handoff_pending ?
                                           &s_pending_old_config : &cfg;
    bridge_init(active_config->bridge_url, active_config->bridge_key);
    ota_init(active_config->bridge_url, active_config->bridge_key);
    photo_frame_network_init(active_config->bridge_url, active_config->bridge_key);

    // WiFi init
    wifi_init(cfg.wifi_ssid, cfg.wifi_pass, POSIX_TZ, NTP_SERVER);
    if (has_saved_prefs) { wifi_set_timezone(saved_prefs.posix_timezone); photo_frame_confirm_timezone(saved_prefs.timezone, saved_prefs.posix_timezone); }

    vTaskDelay(pdMS_TO_TICKS(1000));
    if (s_network_handoff_pending) {
        if (wifi_is_connected() &&
            !web_sync_ack(s_pending_old_config.bridge_url, s_pending_old_config.bridge_key,
                          FW_VERSION, s_boot_id))
            web_sync_set_error("old_bridge_ack_unavailable");
        finalize_network_handoff(&s_pending_new_config, false);
    }

    const uint32_t TICK_MS = 10000;
    uint32_t bridge_elapsed = BRIDGE_UPDATE_INTERVAL_MS; // trigger immediately
    uint32_t ota_elapsed = OTA_CHECK_INTERVAL_MS;        // trigger immediately
    uint32_t sync_elapsed = 10000;                       // report immediately
    bool cal_initial = true;
    bool app_validated = false;
    for (;;) {
        halt_if_settings_fail_closed();
        network_request_t req = {};
        while (xQueueReceive(s_request_queue, &req, 0) == pdTRUE) {
            if (req.kind == REQUEST_RECOVERY_CONFIG) complete_recovery_request(&req);
            else if (req.kind == REQUEST_CALENDAR) bridge_fetch_calendar(req.year, req.month, req.day);
            else if (req.kind == REQUEST_TOGGLE) bridge_toggle_light(req.entity);
            else if (req.kind == REQUEST_CLIMATE) { if (req.action_id) action_run_request(req); else bridge_climate_action(req.entity, req.action, req.temperature, req.mode); }
            else if (req.kind == REQUEST_RECOVERY) recovery_ap_start();
            else if (req.kind == REQUEST_RECOVERY_STOP) recovery_ap_stop();
            else ota_manual_check();
        }

        s_action_executor.poll();

        // Bootstrap identity and preferences before requesting profile-scoped data.
        if (sync_elapsed >= 10000 && wifi_is_connected()) {
            web_sync_response_t sync_response = {};
            if (web_sync_poll(cfg.bridge_url, cfg.bridge_key, FW_VERSION,
                              s_boot_id, &sync_response)) {
                if (sync_response.command.id[0]) {
                    run_web_command(&sync_response.command, &cfg, s_boot_id);
                    sync_elapsed = 0;
                } else {
                device_config_t candidate = cfg;
                bool network_changed = false, wifi_trial = false;
                char target_bridge_id[33] = {};
                bool network_ready = stage_network_update(&sync_response.network, &cfg,
                    s_boot_id, &candidate, &network_changed, &wifi_trial,
                    target_bridge_id, sizeof(target_bridge_id));
                halt_if_settings_fail_closed();
                bool applied = network_ready && web_sync_apply_preferences(&sync_response);
                if (network_changed && network_ready && !applied) {
                    if (wifi_trial) wifi_rollback_trial();
                    if (!web_settings_store_network_trial_recover_pending()) {
                        s_settings_fail_closed = true;
                        halt_if_settings_fail_closed();
                    }
                    restore_runtime_preferences();
                }
                if (network_changed && applied) {
                    if (!web_settings_store_network_trial_mark_committed()) {
                        device_config_t old_config = {};
                        web_preferences_t old_preferences = {};
                        bool had_config = false, had_preferences = false, committed = false;
                        if (!web_settings_store_network_trial_read(&old_config, &had_config,
                                &old_preferences, &had_preferences, &committed)) {
                            ESP_LOGE(TAG, "Network journal unreadable after commit error");
                            for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
                        }
                        if (!committed) {
                            if (wifi_trial) wifi_rollback_trial();
                            if (!web_settings_store_network_trial_recover_pending()) {
                                ESP_LOGE(TAG, "Network rollback failed after commit error");
                                for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
                            }
                            restore_runtime_preferences();
                            applied = false;
                        }
                    }
                    if (applied) {
                        if (cfg.wifi_ssid[0] &&
                            !web_sync_ack(cfg.bridge_url, cfg.bridge_key,
                                          FW_VERSION, s_boot_id))
                            web_sync_set_error("old_bridge_ack_unavailable");
                        finalize_network_handoff(&candidate, wifi_trial);
                    }
                }
                if (applied) {
                    web_preferences_t live = {};
                    if (web_settings_store_load(&live)) { wifi_set_timezone(live.posix_timezone); photo_frame_confirm_timezone(live.timezone, live.posix_timezone); }
                }
                }
            }
            sync_elapsed = 0;
        }

        // Bridge polling
        if (bridge_elapsed >= BRIDGE_UPDATE_INTERVAL_MS) {
            bridge_fetch_and_update();
            bridge_elapsed = 0;

            // Mark app as valid after first successful bridge fetch (rollback protection)
            bool has_data = bridge_has_data();
            if (!app_validated && has_data) {
                esp_ota_mark_app_valid_cancel_rollback();
                app_validated = true;
                ESP_LOGI(TAG, "App marked valid (rollback cancelled)");
            }

            // Fetch today's calendar after first successful bridge update
            if (cal_initial && has_data && wifi_time_is_valid()) {
                time_t now;
                time(&now);
                struct tm t;
                localtime_r(&now, &t);
                bridge_fetch_calendar(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
                cal_initial = false;
            }
        }

        // Periodic OTA check
        if (ota_elapsed >= OTA_CHECK_INTERVAL_MS) {
            ota_managed_poll();
            ota_elapsed = 0;
        }

        TickType_t started = xTaskGetTickCount();
        if (xQueueReceive(s_request_queue, &req, pdMS_TO_TICKS(s_action_executor.wait_ms(TICK_MS))) == pdTRUE) {
            if (req.kind == REQUEST_RECOVERY_CONFIG) complete_recovery_request(&req);
            else if (req.kind == REQUEST_CALENDAR) bridge_fetch_calendar(req.year, req.month, req.day);
            else if (req.kind == REQUEST_TOGGLE) bridge_toggle_light(req.entity);
            else if (req.kind == REQUEST_CLIMATE) { if (req.action_id) action_run_request(req); else bridge_climate_action(req.entity, req.action, req.temperature, req.mode); }
            else if (req.kind == REQUEST_RECOVERY) recovery_ap_start();
            else if (req.kind == REQUEST_RECOVERY_STOP) recovery_ap_stop();
            else ota_manual_check();
        }
        uint32_t elapsed = (xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
        bridge_elapsed += elapsed;
        ota_elapsed += elapsed;
        sync_elapsed += elapsed;
    }
}

extern "C" void app_main(void)
{
    // Initialize NVS (required by WiFi and config)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    vTaskDelay(pdMS_TO_TICKS(100));

    static esp_lcd_panel_handle_t panel_handle = NULL;
    static esp_lcd_touch_handle_t tp_handle = NULL;

    // 1. Hardware init
    tp_handle = touch_gt911_init();
    panel_handle = waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();
    // Failure leaves brightness unavailable; display operation continues.
    if (!brightness_runtime_start()) ESP_LOGW(TAG, "Brightness restore worker unavailable");
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

    // 2. Create dashboard UI
    if (lvgl_port_lock(-1)) {
        ui_dashboard_create();
        photo_frame_ui_init();
        lvgl_port_unlock();
    }

    // 3. Start LVGL render loop
    lvgl_port_task_start();

    // 4. Network task on core 0
    s_request_queue = xQueueCreate(8, sizeof(network_request_t));
    configASSERT(s_request_queue);
    xTaskCreatePinnedToCore(network_task, "network", 32 * 1024, NULL, 1, NULL, 0);
}
