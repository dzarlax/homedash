#include "brightness_controller.h"
#include <string.h>

bool brightness_init(brightness_controller_t *b, brightness_write_fn write, void *context,
                     uint8_t low, uint8_t high, uint8_t baseline) {
    if (!b || !write || low > 97 || high > 97 || low == high || baseline > 97) return false;
    memset(b, 0, sizeof(*b));
    b->write = write; b->context = context; b->pwm_at_10 = low; b->pwm_at_100 = high;
    b->baseline = b->current = baseline; b->ready = true;
    return true;
}
static uint8_t mapped(const brightness_controller_t *b, uint8_t percent) {
    int delta = (int)b->pwm_at_100 - b->pwm_at_10;
    int pwm = (int)b->pwm_at_10 + delta * (percent - 10) / 90;
    return (uint8_t)pwm;
}
bool brightness_set(brightness_controller_t *b, uint8_t percent) {
    if (!b || !b->ready || percent < 10 || percent > 100 || b->preview) return false;
    uint8_t pwm = mapped(b, percent);
    if (!b->write(pwm, b->context)) return false;
    b->current = pwm; return true;
}
bool brightness_preview(brightness_controller_t *b, uint8_t percent, uint64_t now_ms) {
    if (!b || !b->ready || b->preview || percent < 10 || percent > 100 || now_ms > UINT64_MAX - 20000) return false;
    uint8_t old = b->current;
    uint8_t pwm = mapped(b, percent);
    if (!b->write(pwm, b->context)) return false;
    b->restore = old; b->current = pwm; b->preview = true; b->deadline_ms = now_ms + 20000;
    return true;
}
bool brightness_restore(brightness_controller_t *b) {
    if (!b || !b->ready) return false;
    if (!b->preview) return true;
    if (!b->write(b->restore, b->context)) return false;
    b->current = b->restore; b->preview = false; b->deadline_ms = 0; return true;
}
bool brightness_tick(brightness_controller_t *b, uint64_t now_ms) {
    return b && b->ready && (!b->preview || now_ms < b->deadline_ms || brightness_restore(b));
}

bool brightness_apply_schedule(brightness_controller_t *b, uint8_t day, uint8_t night,
                               bool is_night, bool wake_override) {
    if (!b || !b->ready || day < 10 || day > 100 || night < 10 || night > 100) return false;
    if (b->preview) return true;
    uint8_t target = is_night && !wake_override ? night : day;
    if (mapped(b, target) == b->current) return true;
    return brightness_set(b, target);
}
