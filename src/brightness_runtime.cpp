#include "brightness_runtime.h"
#include "brightness_controller.h"
#include "io_extension.h"
#include "esp_timer.h"
static brightness_controller_t state = {};
static bool write_failed = false;
static bool output_uncertain = false;
static bool recovery_pending = false;
static brightness_checkpoint_t recovery_checkpoint = {};
static bool write_pwm(uint8_t pwm, void *) {
    bool ok = IO_EXTENSION_Pwm_Try_Output(pwm) == ESP_OK;
    write_failed = !ok;
    output_uncertain = !ok;
    return ok;
}
const char *brightness_runtime_error() { return write_failed ? "write_failed" : ""; }
void brightness_runtime_mark_write_failed() { write_failed = true; }
bool brightness_runtime_apply_profile(const brightness_calibration_profile_t *profile,
                                      const brightness_board_identity_t *observed_board) {
    if (state.ready || recovery_pending) return false;
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
bool brightness_runtime_ready() { return state.ready && !output_uncertain && !recovery_pending; }
bool brightness_runtime_preview(uint8_t percent) {
    if (!brightness_runtime_ready() || percent < 10 || percent > 100) return false;
    brightness_checkpoint_t checkpoint = state;
    if (brightness_preview(&state, percent, (uint64_t)(esp_timer_get_time() / 1000))) return true;
    // A successful rollback confirms the previous output and clears the
    // transient write error. A failed rollback leaves write_failed set and
    // disables the capability until the output can be confirmed again.
    brightness_runtime_rollback(&checkpoint); return false;
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
    if (recovery_pending) return retry_recovery();
    return brightness_restore(&state);
}
bool brightness_runtime_checkpoint(brightness_checkpoint_t *out) {
    if (!out || !brightness_runtime_ready()) return false;
    *out = state; return true;
}
bool brightness_runtime_rollback(const brightness_checkpoint_t *checkpoint) {
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
bool brightness_runtime_schedule(uint8_t day, uint8_t night, bool is_night, bool wake_override) {
    if (!brightness_runtime_ready() || day < 10 || day > 100 || night < 10 || night > 100) return false;
    brightness_checkpoint_t checkpoint = state;
    if (brightness_apply_schedule(&state, day, night, is_night, wake_override)) return true;
    brightness_runtime_rollback(&checkpoint); return false;
}
void brightness_runtime_tick() {
    if (recovery_pending) {
        retry_recovery();
        return;
    }
    if (state.ready) brightness_tick(&state, (uint64_t)(esp_timer_get_time() / 1000));
}
