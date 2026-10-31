#pragma once
#include <stdint.h>

// The caller supplies a validated profile and a known current command.
// This controller does not infer hardware support from a board model.
typedef bool (*brightness_write_fn)(uint8_t pwm, void *context);
struct brightness_controller_t {
    brightness_write_fn write;
    void *context;
    uint8_t pwm_at_10, pwm_at_100, baseline, current, restore;
    uint64_t deadline_ms;
    bool ready, preview;
    bool configured, live_preview, live_expired;
    uint64_t live_limit_ms;
};
bool brightness_init(brightness_controller_t *b, brightness_write_fn write, void *context,
                     uint8_t pwm_at_10, uint8_t pwm_at_100, uint8_t baseline);
bool brightness_set(brightness_controller_t *b, uint8_t percent);
// Reject another preview until restore succeeds; never extend its deadline.
bool brightness_preview(brightness_controller_t *b, uint8_t percent, uint64_t now_ms);
bool brightness_adjust_preview(brightness_controller_t *b, uint8_t percent, uint64_t now_ms);
bool brightness_restore(brightness_controller_t *b);
bool brightness_tick(brightness_controller_t *b, uint64_t now_ms);
// Call with the local-time night decision from the existing scheduler. An active
// preview wins until restored; wake_override selects the day level.
bool brightness_apply_schedule(brightness_controller_t *b, uint8_t day_percent,
                               uint8_t night_percent, bool night, bool wake_override);
