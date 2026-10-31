#include "brightness_runtime.h"
#include "brightness_controller.h"
#include "io_extension.h"
#include "esp_timer.h"
#include "config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static brightness_controller_t state = {};
static SemaphoreHandle_t state_lock = nullptr;
static bool worker_started = false;

// Startup runs once before UI/network callers. The worker never takes LVGL's
// lock; all controller state and writes share this lock, then the I2C lock.
class StateLock {
public:
    StateLock() : held(state_lock && xSemaphoreTake(state_lock, portMAX_DELAY) == pdTRUE) {}
    ~StateLock() { if (held) xSemaphoreGive(state_lock); }
    explicit operator bool() const { return held; }
private:
    bool held;
};
static void restore_worker(void *) {
    for (;;) {
        brightness_runtime_tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
bool brightness_runtime_start() {
    if (!state_lock) state_lock = xSemaphoreCreateMutex();
    StateLock lock;
    if (!lock) return false;
    if (worker_started) return true;
    worker_started = xTaskCreate(restore_worker, "brightness", 3072, nullptr, 2, nullptr) == pdPASS;
    return worker_started;
}
static bool write_failed = false;
static bool output_uncertain = false;
static bool recovery_pending = false;
static brightness_checkpoint_t recovery_checkpoint = {};
static brightness_reference_phase_t reference_phase = BRIGHTNESS_REFERENCE_IDLE;
static uint64_t reference_deadline_ms = 0;
static uint8_t reference_command = 0;
static bool has_last_command = false, has_report_test = false;
static uint8_t last_command = 0, report_test_command = 0, report_return_command = 0;
static uint64_t report_deadline_ms = 0;
static bool reference_active() {
    return reference_phase != BRIGHTNESS_REFERENCE_IDLE;
}
static bool write_pwm(uint8_t pwm, void *) {
    bool ok = IO_EXTENSION_Pwm_Try_Output(pwm) == ESP_OK;
    if (ok) { last_command = pwm; has_last_command = true; }
    write_failed = !ok;
    output_uncertain = !ok;
    return ok;
}
const char *brightness_runtime_error() {
    StateLock lock;
    return lock && write_failed ? "write_failed" : "";
}
void brightness_runtime_mark_write_failed() { StateLock lock; if (lock) write_failed = true; }
static bool ready_locked() { return worker_started && state.ready && !output_uncertain && !recovery_pending && !reference_active(); }
static bool rollback_locked(const brightness_checkpoint_t *checkpoint);
bool brightness_runtime_apply_profile(const brightness_calibration_profile_t *profile,
                                      const brightness_board_identity_t *observed_board) {
    StateLock lock;
    if (!lock || !worker_started || state.ready || recovery_pending || output_uncertain || reference_active()) return false;
    if (!brightness_calibration_validate(profile, observed_board)) return false;
    // Applying a profile records the measured baseline as current state. It
    // deliberately performs no hardware write; callers must use explicit
    // runtime preview, restore, rollback, or schedule operations to change PWM.
    return brightness_init(&state, write_pwm, nullptr, profile->pwm_at_10,
                           profile->pwm_at_100, profile->baseline_pwm);
}
// A failed I2C write leaves the physical output uncertain until a later
// successful restore/write confirms it. Keep automatic restore retries
// available through state.ready, but do not advertise the capability as ready.
// A caller may separately mark a failed higher-level operation after a
// successful rollback; that diagnostic alone does not make the output unknown.
bool brightness_runtime_ready() { StateLock lock; return lock && ready_locked(); }
bool brightness_runtime_preview(uint8_t percent) {
    StateLock lock;
    if (!lock || !ready_locked() || state.preview || percent < 10 || percent > 100) return false;
    brightness_checkpoint_t checkpoint = state;
    if (brightness_preview(&state, percent, (uint64_t)(esp_timer_get_time() / 1000))) {
        has_report_test = true; report_test_command = state.current;
        report_return_command = state.restore; report_deadline_ms = state.deadline_ms;
        return true;
    }
    // A successful rollback confirms the previous output and clears the
    // transient write error. A failed rollback leaves write_failed set and
    // disables the capability until the output can be confirmed again.
    rollback_locked(&checkpoint); return false;
}
static bool retry_recovery() {
    if (!recovery_pending) return false;
    if (!write_pwm(recovery_checkpoint.current, nullptr)) return false;
    state = recovery_checkpoint;
    recovery_checkpoint = {};
    recovery_pending = false;
    return true;
}
bool brightness_runtime_restore() {
    StateLock lock;
    if (!lock || !worker_started) return false;
    if (reference_active()) {
        bool ok = write_pwm(50, nullptr);
        reference_phase = ok ? BRIGHTNESS_REFERENCE_RESTORED : BRIGHTNESS_REFERENCE_ERROR;
        return ok;
    }
    if (recovery_pending) return retry_recovery();
    return brightness_restore(&state);
}
bool brightness_runtime_checkpoint(brightness_checkpoint_t *out) {
    StateLock lock;
    if (!lock || !out || !ready_locked()) return false;
    *out = state; return true;
}
static bool rollback_locked(const brightness_checkpoint_t *checkpoint) {
    if (!checkpoint || !state.ready || !checkpoint->ready) return false;
    recovery_checkpoint = *checkpoint;
    recovery_pending = true;
    if (!write_pwm(recovery_checkpoint.current, nullptr)) {
        // Preserve the exact known output and controller state for explicit or
        // monotonic-tick retry. Do not derive a replacement from a profile.
        state.ready = false;
        return false;
    }
    state = *checkpoint;
    recovery_checkpoint = {};
    recovery_pending = false;
    return true;
}
bool brightness_runtime_rollback(const brightness_checkpoint_t *checkpoint) {
    StateLock lock;
    return lock && worker_started && rollback_locked(checkpoint);
}
bool brightness_runtime_schedule(uint8_t day, uint8_t night, bool is_night, bool wake_override) {
    StateLock lock;
    if (!lock || !ready_locked() || day < 10 || day > 100 || night < 10 || night > 100) return false;
    brightness_checkpoint_t checkpoint = state;
    if (brightness_apply_schedule(&state, day, night, is_night, wake_override)) return true;
    rollback_locked(&checkpoint); return false;
}
void brightness_runtime_tick() {
    StateLock lock;
    if (!lock || !worker_started) return;
    if (reference_phase == BRIGHTNESS_REFERENCE_ERROR ||
        (reference_phase == BRIGHTNESS_REFERENCE_PREVIEW &&
         (uint64_t)(esp_timer_get_time() / 1000) >= reference_deadline_ms)) {
        reference_phase = write_pwm(50, nullptr) ? BRIGHTNESS_REFERENCE_RESTORED : BRIGHTNESS_REFERENCE_ERROR;
        return;
    }
    if (recovery_pending) {
        retry_recovery();
        return;
    }
    if (state.ready) brightness_tick(&state, (uint64_t)(esp_timer_get_time() / 1000));
}

brightness_reference_status_t brightness_runtime_reference_status() {
    StateLock lock;
    if (!HOMEDASH_BRIGHTNESS_REFERENCE_TEST || !lock || !worker_started)
        return {BRIGHTNESS_REFERENCE_DISABLED, 0, 0};
    uint64_t ms = (uint64_t)(esp_timer_get_time() / 1000);
    uint32_t seconds = reference_phase == BRIGHTNESS_REFERENCE_PREVIEW && reference_deadline_ms > ms ?
        (uint32_t)((reference_deadline_ms - ms + 999) / 1000) : 0;
    return {reference_phase, seconds, reference_command};
}
bool brightness_runtime_reference_baseline() {
    StateLock lock;
    if (!HOMEDASH_BRIGHTNESS_REFERENCE_TEST || !lock || !worker_started || state.ready ||
        recovery_pending || (reference_phase != BRIGHTNESS_REFERENCE_IDLE &&
                             reference_phase != BRIGHTNESS_REFERENCE_RESTORED)) return false;
    bool ok = write_pwm(50, nullptr);
    reference_phase = ok ? BRIGHTNESS_REFERENCE_CONFIRM : BRIGHTNESS_REFERENCE_ERROR;
    return ok;
}
bool brightness_runtime_reference_preview(uint8_t command) {
    StateLock lock;
    if (!HOMEDASH_BRIGHTNESS_REFERENCE_TEST || !lock || !worker_started ||
        (reference_phase != BRIGHTNESS_REFERENCE_CONFIRM && reference_phase != BRIGHTNESS_REFERENCE_RESTORED) ||
        (command != 40 && command != 60 && command != 70) || output_uncertain) return false;
    // An explicit second action confirms that the newly established reference
    // is readable. No measured calibration profile or permanent setting is made.
    reference_deadline_ms = (uint64_t)(esp_timer_get_time() / 1000) + 20000;
    if (!write_pwm(command, nullptr)) {
        reference_phase = write_pwm(50, nullptr) ? BRIGHTNESS_REFERENCE_RESTORED : BRIGHTNESS_REFERENCE_ERROR;
        return false;
    }
    has_report_test = true; report_test_command = command; report_return_command = 50;
    report_deadline_ms = reference_deadline_ms;
    reference_command = command;
    reference_phase = BRIGHTNESS_REFERENCE_PREVIEW;
    return true;
}

brightness_report_t brightness_runtime_report() {
    StateLock lock;
    brightness_report_t r = {};
    r.phase = "unknown"; r.error = "";
    r.sample_uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
    if (!lock || !worker_started) return r;
    r.has_last_command = has_last_command; r.last_command = last_command;
    r.output_known = has_last_command && !output_uncertain && !recovery_pending;
    r.has_test = has_report_test; r.test_command = report_test_command;
    r.return_command = report_return_command; r.restore_deadline_uptime_ms = report_deadline_ms;
    if (output_uncertain || recovery_pending) { r.phase = "error"; r.error = "write_failed"; }
    else if (has_last_command) {
        if (reference_phase == BRIGHTNESS_REFERENCE_PREVIEW || state.preview) r.phase = "preview";
        else if (reference_phase == BRIGHTNESS_REFERENCE_CONFIRM) r.phase = "reference";
        else r.phase = has_report_test && last_command == report_return_command ? "restored" : "reference";
    }
    return r;
}
