#include "brightness_calibration.h"
#include <string.h>

static bool nonempty(const char *value) {
    return value && value[0] != '\0';
}

bool brightness_calibration_validate(const brightness_calibration_profile_t *profile,
                                     const brightness_board_identity_t *observed_board) {
    if (!profile || !observed_board || profile->format_version != 1 ||
        profile->profile_revision == 0 || !nonempty(profile->board_id) ||
        !nonempty(profile->board_revision) || !nonempty(observed_board->board_id) ||
        !nonempty(observed_board->board_revision) || !profile->baseline_verified ||
        !nonempty(profile->baseline_evidence)) return false;

    if (strcmp(profile->board_id, observed_board->board_id) != 0 ||
        strcmp(profile->board_revision, observed_board->board_revision) != 0) return false;

    // This driver caps the expander's raw PWM command at 97. The measured safe
    // range must fit inside that cap, and all configured values must fit inside
    // the measured range.
    if (profile->pwm_min > profile->pwm_max || profile->pwm_max > 97 ||
        profile->pwm_at_10 < profile->pwm_min || profile->pwm_at_10 > profile->pwm_max ||
        profile->pwm_at_100 < profile->pwm_min || profile->pwm_at_100 > profile->pwm_max ||
        profile->baseline_pwm < profile->pwm_min || profile->baseline_pwm > profile->pwm_max ||
        profile->pwm_at_10 == profile->pwm_at_100) return false;
    return true;
}
