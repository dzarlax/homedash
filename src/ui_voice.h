#pragma once

#include "lvgl.h"
#include <stdbool.h>

typedef enum {
    UI_VOICE_LISTENING,
    UI_VOICE_PROCESSING,
    UI_VOICE_LOOKUP,
    UI_VOICE_ANSWER,
    UI_VOICE_APPROVAL_REQUIRED,
    UI_VOICE_ERROR,
    UI_VOICE_OFFLINE,
} ui_voice_phase_t;

typedef struct {
    ui_voice_phase_t phase;
    const char *text;       // Spoken response only; caller owns source memory.
    bool interrupted;
    bool text_truncated;
} ui_voice_state_t;

typedef void (*ui_voice_dismiss_cb_t)(void *context);
typedef void (*ui_voice_activity_cb_t)(void *context);

// Create once under the LVGL lock. parent should be the active display root.
void ui_voice_create(lv_obj_t *parent, ui_voice_dismiss_cb_t dismiss,
                     ui_voice_activity_cb_t activity, void *context);
void ui_voice_show(const ui_voice_state_t *state);
void ui_voice_update(const ui_voice_state_t *state);
void ui_voice_hide(void);
// Hide immediately for priority transitions such as OTA or local recovery.
void ui_voice_hide_immediately(void);
bool ui_voice_is_visible(void);

#if defined(SIMULATOR)
const char *ui_voice_sim_status_text(void);
const char *ui_voice_sim_answer_text(void);
const char *ui_voice_sim_speech_text(void);
const char *ui_voice_sim_truncated_text(void);
bool ui_voice_sim_at_bottom(void);
void ui_voice_sim_scroll_up(void);
bool ui_voice_sim_physically_hidden(void);
#endif
