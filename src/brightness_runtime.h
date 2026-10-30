#pragma once
#include <stdint.h>
#include "brightness_controller.h"
#include "brightness_calibration.h"
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
void brightness_runtime_tick();
