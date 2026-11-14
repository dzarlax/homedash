#include "ui_voice.h"
#include "config.h"
#include "ui_strings.h"
#include <cstring>
#include <lvgl.h>

LV_FONT_DECLARE(font_montserrat_16_cyr);
LV_FONT_DECLARE(font_montserrat_24_cyr);

namespace {
// Match the dashboard palette without depending on its private macros.
constexpr uint32_t kBackground = 0xF4F1EA;
constexpr uint32_t kAccent = 0xD7E4DA;
constexpr uint32_t kText = 0x24352E;
constexpr uint32_t kMuted = 0x6B7970;
constexpr uint32_t kGreen = 0x4F7564;

lv_obj_t *root;
lv_obj_t *orb;
lv_obj_t *status_label;
lv_obj_t *speech_label;
lv_obj_t *truncated_label;
lv_obj_t *answer_label;
lv_obj_t *scroll_area;
lv_obj_t *title_label;
lv_obj_t *return_label;
lv_obj_t *footer_label;
lv_obj_t *orb_core;
ui_voice_dismiss_cb_t dismiss_cb;
ui_voice_activity_cb_t activity_cb;
void *callback_context;
bool manually_scrolled;
bool updating_scroll;
bool hiding;

constexpr uint32_t kTransitionMs = 180;
constexpr uint32_t kOrbCycleMs = 2400;

const char *phase_text(ui_voice_phase_t phase) {
    const bool en = !strcmp(ui_strings_locale(), "en");
    const bool sr = !strcmp(ui_strings_locale(), "sr-Latn");
    switch (phase) {
    case UI_VOICE_LISTENING: return en ? "Listening" : sr ? "Slušam" : "Слушаю";
    case UI_VOICE_PROCESSING: return en ? "Working on your request" : sr ? "Obrađujem zahtev" : "Обрабатываю запрос";
    case UI_VOICE_LOOKUP: return en ? "Looking for information" : sr ? "Tražim informacije" : "Ищу информацию";
    case UI_VOICE_ANSWER: return en ? "Assistant speech" : sr ? "Govor asistenta" : "Речь ассистента";
    case UI_VOICE_APPROVAL_REQUIRED: return en ? "Approval needed" : sr ? "Potrebna je potvrda" : "Требуется подтверждение";
    case UI_VOICE_ERROR: return en ? "Connection interrupted" : sr ? "Veza je prekinuta" : "Связь прервана";
    case UI_VOICE_OFFLINE: return en ? "Connection interrupted" : sr ? "Veza je prekinuta" : "Связь прервана";
    }
    return ui_text("Обрабатываю запрос");
}

const char *truncated_text() {
    const char *locale = ui_strings_locale();
    return !strcmp(locale, "en") ? "Response shortened" :
           !strcmp(locale, "sr-Latn") ? "Odgovor je skraćen" : "Ответ сокращён";
}

const char *speech_text() {
    const char *locale = ui_strings_locale();
    return !strcmp(locale, "en") ? "Assistant speech" :
           !strcmp(locale, "sr-Latn") ? "Govor asistenta" : "Речь ассистента";
}

void refresh_static_labels() {
    if (!title_label) return;
    const char *locale = ui_strings_locale();
    lv_label_set_text(title_label, !strcmp(locale, "en") ? "ASSISTANT" : !strcmp(locale, "sr-Latn") ? "ASISTENT" : "АССИСТЕНТ");
    lv_label_set_text(return_label, !strcmp(locale, "en") ? "Return to dashboard" : !strcmp(locale, "sr-Latn") ? "Nazad na kontrolnu tablu" : "Вернуться к дашборду");
    lv_label_set_text(footer_label, !strcmp(locale, "en") ? "Ask another question with the microphone button" : !strcmp(locale, "sr-Latn") ? "Novo pitanje postavite dugmetom na mikrofonu" : "Можно задать следующий вопрос кнопкой на микрофоне");
    if (speech_label && !lv_obj_has_flag(speech_label, LV_OBJ_FLAG_HIDDEN))
        lv_label_set_text(speech_label, speech_text());
    if (truncated_label && !lv_obj_has_flag(truncated_label, LV_OBJ_FLAG_HIDDEN))
        lv_label_set_text(truncated_label, truncated_text());
}

void set_root_opacity(void *object, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void finish_hide(lv_anim_t *animation) {
    auto *object = static_cast<lv_obj_t *>(animation->var);
    if (object != root || !hiding) return;
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(root, LV_OPA_COVER, 0);
    hiding = false;
}

void animate_root(bool appearing) {
    if (!root) return;
    lv_anim_del(root, set_root_opacity);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, root);
    lv_anim_set_exec_cb(&animation, set_root_opacity);
    lv_anim_set_values(&animation, appearing ? LV_OPA_TRANSP : LV_OPA_COVER,
                       appearing ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_anim_set_time(&animation, kTransitionMs);
    if (!appearing) lv_anim_set_ready_cb(&animation, finish_hide);
    lv_anim_start(&animation);
}

void animate_orb(void *object, int32_t value) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(object), static_cast<lv_opa_t>(value), 0);
}

void start_orb_animation() {
    if (!orb_core) return;
    lv_anim_del(orb_core, animate_orb);
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, orb_core);
    lv_anim_set_exec_cb(&animation, animate_orb);
    lv_anim_set_values(&animation, 190, LV_OPA_COVER);
    lv_anim_set_time(&animation, kOrbCycleMs / 2);
    lv_anim_set_playback_time(&animation, kOrbCycleMs / 2);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

bool at_bottom() {
    if (!scroll_area) return true;
    const int32_t max = lv_obj_get_scroll_bottom(scroll_area);
    return max <= 2;
}

void on_scroll(lv_event_t *event) {
    if (updating_scroll) return;
    if (!at_bottom()) manually_scrolled = true;
    else manually_scrolled = false;
    if (activity_cb) activity_cb(callback_context);
}

void on_return(lv_event_t *) {
    ui_voice_hide();
    if (dismiss_cb) dismiss_cb(callback_context);
}

void label_style(lv_obj_t *label, uint32_t color, const lv_font_t *font) {
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
}
}

void ui_voice_create(lv_obj_t *parent, ui_voice_dismiss_cb_t dismiss,
                     ui_voice_activity_cb_t activity, void *context) {
    if (root) return;
    if (!parent) parent = lv_screen_active();
    dismiss_cb = dismiss;
    activity_cb = activity;
    callback_context = context;
    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(root, 38, 0);
    lv_obj_set_style_pad_ver(root, 24, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(root, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *top = lv_obj_create(root);
    lv_obj_remove_style_all(top);
    lv_obj_set_width(top, LV_PCT(100));
    lv_obj_set_height(top, 58);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    title_label = lv_label_create(top);
    label_style(title_label, kMuted, &font_montserrat_16_cyr);

    lv_obj_t *button = lv_button_create(top);
    lv_obj_set_height(button, 52);
    lv_obj_set_style_bg_color(button, lv_color_hex(kAccent), 0);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(button, 20, 0);
    lv_obj_add_event_cb(button, on_return, LV_EVENT_CLICKED, nullptr);
    return_label = lv_label_create(button);
    label_style(return_label, kText, &font_montserrat_16_cyr);

    lv_obj_t *middle = lv_obj_create(root);
    lv_obj_remove_style_all(middle);
    lv_obj_set_width(middle, LV_PCT(100));
    lv_obj_set_flex_grow(middle, 1);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(middle, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(middle, 40, 0);

    orb = lv_obj_create(middle);
    lv_obj_remove_style_all(orb);
    lv_obj_set_size(orb, 104, 104);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb, lv_color_hex(kAccent), 0);
    lv_obj_set_style_bg_opa(orb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(orb, 2, 0);
    lv_obj_set_style_border_color(orb, lv_color_hex(0xADC6B5), 0);
    lv_obj_set_style_outline_width(orb, 8, 0);
    lv_obj_set_style_outline_color(orb, lv_color_hex(0xE2E9DF), 0);
    lv_obj_set_style_outline_opa(orb, LV_OPA_COVER, 0);
    orb_core = lv_obj_create(orb);
    lv_obj_remove_style_all(orb_core);
    lv_obj_set_size(orb_core, 38, 38);
    lv_obj_center(orb_core);
    lv_obj_set_style_radius(orb_core, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(orb_core, lv_color_hex(kGreen), 0);
    lv_obj_set_style_bg_opa(orb_core, LV_OPA_COVER, 0);

    lv_obj_t *copy = lv_obj_create(middle);
    lv_obj_remove_style_all(copy);
    lv_obj_set_flex_grow(copy, 1);
    lv_obj_set_height(copy, LV_PCT(100));
    lv_obj_set_flex_flow(copy, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_ver(copy, 8, 0);
    status_label = lv_label_create(copy);
    label_style(status_label, kGreen, &font_montserrat_16_cyr);
    speech_label = lv_label_create(copy);
    label_style(speech_label, kMuted, &font_montserrat_16_cyr);
    lv_obj_add_flag(speech_label, LV_OBJ_FLAG_HIDDEN);
    truncated_label = lv_label_create(copy);
    label_style(truncated_label, kMuted, &font_montserrat_16_cyr);
    lv_obj_add_flag(truncated_label, LV_OBJ_FLAG_HIDDEN);
    scroll_area = lv_obj_create(copy);
    lv_obj_set_width(scroll_area, LV_PCT(100));
    lv_obj_set_flex_grow(scroll_area, 1);
    lv_obj_set_style_bg_opa(scroll_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll_area, 0, 0);
    lv_obj_set_style_pad_all(scroll_area, 0, 0);
    lv_obj_set_scroll_dir(scroll_area, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll_area, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(scroll_area, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(scroll_area, on_scroll, LV_EVENT_SCROLL, nullptr);
    answer_label = lv_label_create(scroll_area);
    lv_obj_set_width(answer_label, LV_PCT(100));
    lv_label_set_long_mode(answer_label, LV_LABEL_LONG_WRAP);
    label_style(answer_label, kText, &font_montserrat_24_cyr);
    footer_label = lv_label_create(root);
    label_style(footer_label, kMuted, &font_montserrat_16_cyr);
    refresh_static_labels();
}

void ui_voice_show(const ui_voice_state_t *state) {
    if (!root) ui_voice_create(nullptr, nullptr, nullptr, nullptr);
    manually_scrolled = false;
    hiding = false;
    lv_anim_del(root, set_root_opacity);
    lv_anim_del(orb_core, animate_orb);
    lv_obj_set_style_opa(root, LV_OPA_TRANSP, 0);
    ui_voice_update(state);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    animate_root(true);
    start_orb_animation();
}

void ui_voice_update(const ui_voice_state_t *state) {
    if (!root || !state) return;
    refresh_static_labels();
    const bool was_bottom = !manually_scrolled && at_bottom();
    lv_label_set_text(status_label, phase_text(state->phase));
    // LVGL owns its copy. Input is expected to have passed protocol UTF-8/size validation.
    const char *text = (state->phase == UI_VOICE_ERROR || state->phase == UI_VOICE_OFFLINE || state->phase == UI_VOICE_APPROVAL_REQUIRED) ? "" : (state->text ? state->text : "");
    const bool neutral_phase = state->phase == UI_VOICE_ERROR || state->phase == UI_VOICE_OFFLINE ||
                               state->phase == UI_VOICE_APPROVAL_REQUIRED;
    const bool show_speech = *text != '\0' && state->phase != UI_VOICE_ANSWER;
    if (show_speech) {
        lv_label_set_text(speech_label, speech_text());
        lv_obj_clear_flag(speech_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(speech_label, "");
        lv_obj_add_flag(speech_label, LV_OBJ_FLAG_HIDDEN);
    }
    const bool show_truncated = state->text_truncated && !neutral_phase;
    if (show_truncated) {
        lv_label_set_text(truncated_label, truncated_text());
        lv_obj_clear_flag(truncated_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(truncated_label, "");
        lv_obj_add_flag(truncated_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(answer_label, text);
    lv_obj_update_layout(scroll_area);
    if (was_bottom) {
        updating_scroll = true;
        lv_obj_scroll_to_y(scroll_area, lv_obj_get_scroll_y(scroll_area) + lv_obj_get_scroll_bottom(scroll_area), LV_ANIM_OFF);
        updating_scroll = false;
        manually_scrolled = false;
    }
}

void ui_voice_hide(void) {
    if (!root || hiding) return;
    // Remove response content before any animated exit so it cannot be exposed.
    lv_label_set_text(answer_label, "");
    lv_label_set_text(speech_label, "");
    lv_obj_add_flag(speech_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(truncated_label, "");
    lv_obj_add_flag(truncated_label, LV_OBJ_FLAG_HIDDEN);
    lv_anim_del(orb_core, animate_orb);
    hiding = true;
    animate_root(false);
}

void ui_voice_hide_immediately(void) {
    if (!root) return;
    lv_anim_del(root, set_root_opacity);
    lv_anim_del(orb_core, animate_orb);
    hiding = false;
    lv_label_set_text(answer_label, "");
    lv_label_set_text(speech_label, "");
    lv_obj_add_flag(speech_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(truncated_label, "");
    lv_obj_add_flag(truncated_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(root, LV_OPA_COVER, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

bool ui_voice_is_visible(void) {
    return root && !hiding && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN);
}

#if defined(SIMULATOR)
const char *ui_voice_sim_status_text(void) { return status_label ? lv_label_get_text(status_label) : ""; }
const char *ui_voice_sim_answer_text(void) { return answer_label ? lv_label_get_text(answer_label) : ""; }
const char *ui_voice_sim_speech_text(void) { return speech_label && !lv_obj_has_flag(speech_label, LV_OBJ_FLAG_HIDDEN) ? lv_label_get_text(speech_label) : ""; }
const char *ui_voice_sim_truncated_text(void) { return truncated_label && !lv_obj_has_flag(truncated_label, LV_OBJ_FLAG_HIDDEN) ? lv_label_get_text(truncated_label) : ""; }
bool ui_voice_sim_at_bottom(void) { return at_bottom(); }
void ui_voice_sim_scroll_up(void) { if (scroll_area) lv_obj_scroll_by(scroll_area, 0, 180, LV_ANIM_OFF); }
bool ui_voice_sim_physically_hidden(void) { return !root || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN); }
#endif
