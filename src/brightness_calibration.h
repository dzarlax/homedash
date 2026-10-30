#pragma once

#include <stdint.h>

// A software description of measurements made on one identified physical
// board. This is not populated from the board model or detected automatically.
struct brightness_board_identity_t {
    const char *board_id;
    const char *board_revision;
};

struct brightness_calibration_profile_t {
    uint16_t format_version;
    uint32_t profile_revision;
    const char *board_id;
    const char *board_revision;
    uint8_t pwm_min;
    uint8_t pwm_max;
    uint8_t pwm_at_10;
    uint8_t pwm_at_100;
    uint8_t baseline_pwm;
    bool baseline_verified;
    const char *baseline_evidence;
};

// Accept only a known profile format for the exact measured board identity,
// with a verified baseline and a non-degenerate mapping inside measured limits.
bool brightness_calibration_validate(const brightness_calibration_profile_t *profile,
                                     const brightness_board_identity_t *observed_board);
