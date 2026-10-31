#pragma once
#include <stdint.h>
#include "brightness_controller.h"
#include "brightness_calibration.h"
#include "brightness_instance_profile.h"
// Call once during startup, before UI/network tasks. Starts a local restore
// worker without enabling brightness or writing PWM. Failure keeps it disabled.
bool brightness_runtime_start();
// Must be called only after explicit physical validation of polarity and a
// trustworthy current PWM baseline. No boot path calls this by default.
bool brightness_runtime_apply_profile(const brightness_calibration_profile_t *profile,
                                      const brightness_board_identity_t *observed_board);
bool brightness_runtime_apply_instance(const brightness_instance_profile_t *, const char *device_id);
bool brightness_runtime_ready();
bool brightness_runtime_settings_confirmed();
bool brightness_runtime_preview_active();
bool brightness_runtime_adjust(uint8_t percent);
bool brightness_runtime_clear_schedule();
const char *brightness_runtime_error();
void brightness_runtime_mark_write_failed();
bool brightness_runtime_preview(uint8_t percent);
bool brightness_runtime_restore();
typedef brightness_controller_t brightness_checkpoint_t;
bool brightness_runtime_checkpoint(brightness_checkpoint_t *out);
bool brightness_runtime_rollback(const brightness_checkpoint_t *checkpoint);
bool brightness_runtime_schedule(uint8_t day, uint8_t night, bool is_night, bool wake_override);
// Worker step; does not call LVGL or use the network.
void brightness_runtime_tick();

// Explicit diagnostic levels, available only in the dedicated test build.
// Reference 50; diagnostic commands 1..97 are not luminance percentages.
enum brightness_reference_phase_t { BRIGHTNESS_REFERENCE_DISABLED, BRIGHTNESS_REFERENCE_IDLE,
    BRIGHTNESS_REFERENCE_CONFIRM, BRIGHTNESS_REFERENCE_PREVIEW,
    BRIGHTNESS_REFERENCE_RESTORED, BRIGHTNESS_REFERENCE_ERROR };
struct brightness_reference_status_t { brightness_reference_phase_t phase; uint32_t remaining_seconds; uint8_t test_command; };
brightness_reference_status_t brightness_runtime_reference_status();
bool brightness_runtime_reference_baseline();
struct brightness_comparison_t {
    uint64_t id;
    bool armed, valid, has_previous, historical, step_failed;
    uint8_t previous, current;
    bool has_bright, has_dark;
    uint8_t bright, dark;
};
brightness_comparison_t brightness_runtime_comparison();
bool brightness_runtime_reference_begin();
bool brightness_runtime_reference_step(uint64_t id, bool brighter, uint8_t step);
bool brightness_runtime_reference_mark(uint64_t id, bool previous, bool dark, bool historical);
// Cancels diagnostics only; a normal calibrated preview is not affected.
bool brightness_runtime_reference_end();

// Atomic command-state snapshot, not sensor feedback or PWM register readback.
struct brightness_report_t {
    const char *phase;
    bool has_last_command, output_known, has_test;
    uint8_t last_command, test_command, return_command;
    uint64_t sample_uptime_ms, restore_deadline_uptime_ms;
    const char *error;
};
brightness_report_t brightness_runtime_report();
