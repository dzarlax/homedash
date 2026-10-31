#pragma once
#include <stdint.h>
#include <string.h>

// User-observed limits for one software identity, not a factory revision or
// photometric measurement. A reset or different identity does not inherit it.
struct brightness_instance_profile_t {
    const char *device_id;
    const char *model;
    uint8_t dim_command, bright_command, reference;
    const char *evidence;
};
inline const brightness_instance_profile_t *brightness_instance_profile_for(const char *id) {
    static const brightness_instance_profile_t verified = {
        "6a36f507bf145264d1be1479864ad342", "waveshare-esp32-s3-touch-lcd-7b", 97, 1, 50,
        "Owner confirmed commands 1..97 and readable reference 50 on 2026-10-31"
    };
    return id && !strcmp(id, verified.device_id) ? &verified : nullptr;
}
