#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>

typedef void (*ui_voice_pair_action_cb_t)(void *context);

// Create once under the LVGL lock. This screen never starts pairing itself.
void ui_voice_pair_create(lv_obj_t *parent, ui_voice_pair_action_cb_t start,
                          ui_voice_pair_action_cb_t cancel, void *context);
void ui_voice_pair_show(void);
// Render a coordinator-owned pairing code (1..199 bytes), status and expiry.
// The caller owns session generation, expiry, and authorization.
bool ui_voice_pair_show_code(const char *code, size_t code_len,
                             const char *status, const char *expiry);
void ui_voice_pair_set_status(const char *status, const char *expiry);
// Clear the code while leaving the result visible until the user closes it.
void ui_voice_pair_finish(const char *status);
// End a failed attempt and allow the owner to try again.
void ui_voice_pair_error(const char *status);
// Hide and clear displayed secret material. No network or session operation.
void ui_voice_pair_hide(void);
bool ui_voice_pair_is_visible(void);

#if defined(SIMULATOR)
const char *ui_voice_pair_sim_code_text(void);
bool ui_voice_pair_sim_has_qr(void);
bool ui_voice_pair_sim_start_visible(void);
bool ui_voice_pair_sim_is_visible(void);
#endif
