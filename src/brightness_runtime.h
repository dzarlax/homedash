#pragma once
#include <stdint.h>
#include "brightness_controller.h"
#include "brightness_calibration.h"
// Call once during startup, before UI/network tasks. Starts a local restore
// worker without enabling brightness or writing PWM. Failure keeps it disabled.
bool brightness_runtime_start();
// Must be called only after explicit physical validation of polarity and a
// trustworthy current PWM baseline. No boot path calls this by default.
bool brightness_runtime_apply_profile(const brightness_calibration_profile_t *profile,
                                      const brightness_board_identity_t *observed_board);
bool brightness_runtime_ready();
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

// Explicit two-level diagnostic, available only in the dedicated test build.
// Values 50/60 are driver commands, never measured luminance percentages.
enum brightness_reference_phase_t { BRIGHTNESS_REFERENCE_DISABLED, BRIGHTNESS_REFERENCE_IDLE,
    BRIGHTNESS_REFERENCE_CONFIRM, BRIGHTNESS_REFERENCE_PREVIEW,
    BRIGHTNESS_REFERENCE_RESTORED, BRIGHTNESS_REFERENCE_ERROR };
struct brightness_reference_status_t { brightness_reference_phase_t phase; uint32_t remaining_seconds; };
brightness_reference_status_t brightness_runtime_reference_status();
bool brightness_runtime_reference_baseline();
bool brightness_runtime_reference_preview();
