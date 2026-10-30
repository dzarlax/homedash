#include "brightness_runtime.h"
#include "brightness_controller.h"
#include "io_extension.h"
#include "esp_timer.h"
static brightness_controller_t state = {};
static bool write_failed = false;
static bool write_pwm(uint8_t pwm, void *) {
    bool ok = IO_EXTENSION_Pwm_Try_Output(pwm) == ESP_OK;
    write_failed = !ok; return ok;
}
const char *brightness_runtime_error() { return write_failed ? "write_failed" : ""; }
void brightness_runtime_mark_write_failed() { write_failed = true; }
bool brightness_runtime_configure_verified(uint8_t low, uint8_t high, uint8_t baseline) {
    if (state.ready) return false;
    return brightness_init(&state, write_pwm, nullptr, low, high, baseline);
}
bool brightness_runtime_ready() { return state.ready; }
bool brightness_runtime_preview(uint8_t percent) {
    if (!state.ready || percent < 10 || percent > 100) return false;
    brightness_checkpoint_t checkpoint = state;
    if (brightness_preview(&state, percent, (uint64_t)(esp_timer_get_time() / 1000))) return true;
    brightness_runtime_rollback(&checkpoint); brightness_runtime_mark_write_failed(); return false;
}
bool brightness_runtime_restore() { return brightness_restore(&state); }
bool brightness_runtime_checkpoint(brightness_checkpoint_t *out) {
    if (!out || !state.ready) return false;
    *out = state; return true;
}
bool brightness_runtime_rollback(const brightness_checkpoint_t *checkpoint) {
    if (!checkpoint || !state.ready || !checkpoint->ready) return false;
    if (!write_pwm(checkpoint->current, nullptr)) { state.ready = false; return false; }
    state = *checkpoint; return true;
}
bool brightness_runtime_schedule(uint8_t day, uint8_t night, bool is_night, bool wake_override) {
    if (!state.ready || day < 10 || day > 100 || night < 10 || night > 100) return false;
    brightness_checkpoint_t checkpoint = state;
    if (brightness_apply_schedule(&state, day, night, is_night, wake_override)) return true;
    brightness_runtime_rollback(&checkpoint); brightness_runtime_mark_write_failed(); return false;
}
void brightness_runtime_tick() { if (state.ready) brightness_tick(&state, (uint64_t)(esp_timer_get_time() / 1000)); }
