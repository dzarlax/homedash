#include "ui_voice_pair.h"
#include "ui_strings.h"
#include <cstring>

LV_FONT_DECLARE(font_montserrat_16_cyr);
LV_FONT_DECLARE(font_montserrat_24_cyr);

namespace {
constexpr size_t kMaxCodeBytes = 199;
constexpr uint32_t kBackground = 0xF4F1EA;
constexpr uint32_t kText = 0x24352E;
constexpr uint32_t kMuted = 0x6B7970;
constexpr uint32_t kAccent = 0xD7E4DA;
constexpr uint32_t kGreen = 0x4F7564;
constexpr uint32_t kQrSize = 264;

lv_obj_t *root, *qr, *code_label, *status_label, *expiry_label, *start_button;
ui_voice_pair_action_cb_t start_cb, cancel_cb;
void *callback_context;
char code_storage[kMaxCodeBytes + 1];
bool pairing_started;

const char *locale() { return ui_strings_locale(); }
bool is_en() { return !strcmp(locale(), "en"); }
bool is_sr() { return !strcmp(locale(), "sr-Latn"); }
const char *title_text() { return is_en() ? "Pair voice assistant" : is_sr() ? "Uparite glasovnog asistenta" : "Подключение голосового помощника"; }
const char *explanation_text() { return is_en() ? "Start only when you are ready to pair this device." : is_sr() ? "Pokrenite uparivanje kada budete spremni." : "Запустите подключение, когда будете готовы."; }
const char *start_text() { return is_en() ? "Start pairing" : is_sr() ? "Pokreni uparivanje" : "Начать подключение"; }
const char *cancel_text() { return is_en() ? "Cancel" : is_sr() ? "Otkaži" : "Отмена"; }
const char *manual_text() { return is_en() ? "Manual pairing code" : is_sr() ? "Kod za ručno uparivanje" : "Код для ручного подключения"; }
const char *default_status() { return is_en() ? "Scan the code or enter it manually." : is_sr() ? "Skenirajte kod ili ga unesite ručno." : "Отсканируйте код или введите его вручную."; }

void set_font(lv_obj_t *obj, const lv_font_t *font, uint32_t color) {
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
}

void wipe_code() {
    if (qr) {
        // Clear the owned QR pixels before LVGL frees its canvas allocation.
        lv_draw_buf_t *draw_buf = lv_canvas_get_draw_buf(qr);
        if (draw_buf && draw_buf->data) {
            volatile uint8_t *pixels = draw_buf->data;
            for (uint32_t i = 0; i < draw_buf->data_size; ++i) pixels[i] = 0;
        }
        lv_obj_delete(qr);
        qr = nullptr;
    }
    volatile char *p = code_storage;
    for (size_t i = 0; i < sizeof(code_storage); ++i) p[i] = 0;
    if (code_label) lv_label_set_text_static(code_label, "");
}

void on_start(lv_event_t *) {
    if (pairing_started) return;
    pairing_started = true;
    if (start_button) lv_obj_add_flag(start_button, LV_OBJ_FLAG_HIDDEN);
    if (status_label) lv_label_set_text(status_label, is_en() ? "Waiting for pairing code…" : is_sr() ? "Čekam kod za uparivanje…" : "Ожидание кода подключения…");
    if (start_cb) start_cb(callback_context);
}

void on_cancel(lv_event_t *) {
    wipe_code();
    if (root) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    pairing_started = false;
    if (start_button) lv_obj_clear_flag(start_button, LV_OBJ_FLAG_HIDDEN);
    if (status_label) lv_label_set_text(status_label, "");
    if (expiry_label) lv_label_set_text(expiry_label, "");
    if (cancel_cb) cancel_cb(callback_context);
}
}

void ui_voice_pair_create(lv_obj_t *parent, ui_voice_pair_action_cb_t start,
                          ui_voice_pair_action_cb_t cancel, void *context) {
    if (root) return;
    if (!parent) parent = lv_screen_active();
    start_cb = start; cancel_cb = cancel; callback_context = context;
    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root, lv_color_hex(kBackground), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root, 24, 0);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(root, LV_FLEX_ALIGN_SPACE_BETWEEN, 0);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(root); lv_label_set_text(title, title_text());
    set_font(title, &font_montserrat_24_cyr, kText);
    lv_obj_t *explain = lv_label_create(root); lv_label_set_text(explain, explanation_text());
    set_font(explain, &font_montserrat_16_cyr, kMuted);

    lv_obj_t *body = lv_obj_create(root); lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100)); lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(body, 30, 0);
    qr = nullptr; // QR is intentionally created only after explicit consent.
    lv_obj_t *right = lv_obj_create(body); lv_obj_remove_style_all(right);
    lv_obj_set_width(right, LV_PCT(58)); lv_obj_set_height(right, LV_PCT(100));
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_flex_main_place(right, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_row(right, 8, 0);
    status_label = lv_label_create(right); lv_label_set_text(status_label, "");
    set_font(status_label, &font_montserrat_16_cyr, kGreen);
    expiry_label = lv_label_create(right); lv_label_set_text(expiry_label, "");
    set_font(expiry_label, &font_montserrat_16_cyr, kMuted);
    lv_obj_t *manual = lv_label_create(right); lv_label_set_text(manual, manual_text());
    set_font(manual, &font_montserrat_16_cyr, kMuted);
    code_label = lv_label_create(right); lv_obj_set_width(code_label, LV_PCT(100));
    lv_label_set_long_mode(code_label, LV_LABEL_LONG_WRAP); lv_label_set_text_static(code_label, "");
    set_font(code_label, &lv_font_montserrat_14, kText);

    start_button = lv_button_create(root); lv_obj_set_height(start_button, 58);
    lv_obj_set_width(start_button, 250); lv_obj_set_style_bg_color(start_button, lv_color_hex(kAccent), 0);
    lv_obj_add_event_cb(start_button, on_start, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *start_label = lv_label_create(start_button); lv_label_set_text(start_label, start_text());
    set_font(start_label, &font_montserrat_16_cyr, kText);
    lv_obj_t *cancel_button = lv_button_create(root); lv_obj_set_height(cancel_button, 54); lv_obj_set_width(cancel_button, 180);
    lv_obj_set_style_bg_color(cancel_button, lv_color_hex(kAccent), 0);
    lv_obj_add_event_cb(cancel_button, on_cancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancel_label = lv_label_create(cancel_button); lv_label_set_text(cancel_label, cancel_text());
    set_font(cancel_label, &font_montserrat_16_cyr, kText);
}

void ui_voice_pair_show(void) {
    if (!root) ui_voice_pair_create(nullptr, nullptr, nullptr, nullptr);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
}

bool ui_voice_pair_show_code(const char *code, size_t code_len,
                             const char *status, const char *expiry) {
    if (!root || !pairing_started || !code || code_len == 0 || code_len > kMaxCodeBytes) return false;
    for (size_t i = 0; i < code_len; ++i) {
        const unsigned char ch = static_cast<unsigned char>(code[i]);
        if (ch < 0x21 || ch > 0x7e) return false;
    }
    // A new valid code replaces all visuals and owned storage from the prior
    // session before its bytes are copied.
    wipe_code();
    memcpy(code_storage, code, code_len); code_storage[code_len] = '\0';
    lv_obj_t *body = lv_obj_get_child(root, 2);
    qr = lv_qrcode_create(body);
    if (!qr) {
        wipe_code();
        if (status_label) lv_label_set_text(status_label, is_en() ? "Code could not be displayed." : is_sr() ? "Kod nije moguće prikazati." : "Не удалось показать код.");
        return false;
    }
    lv_qrcode_set_size(qr, kQrSize);
    lv_qrcode_set_dark_color(qr, lv_color_hex(kText));
    lv_qrcode_set_light_color(qr, lv_color_white());
    if (lv_qrcode_update(qr, code_storage, code_len) != LV_RESULT_OK) {
        wipe_code();
        if (status_label) lv_label_set_text(status_label, is_en() ? "Code could not be displayed." : is_sr() ? "Kod nije moguće prikazati." : "Не удалось показать код.");
        return false;
    }
    lv_obj_set_size(qr, kQrSize, kQrSize);
    lv_label_set_text_static(code_label, code_storage);
    lv_label_set_text(status_label, status && *status ? status : default_status());
    lv_label_set_text(expiry_label, expiry ? expiry : "");
    lv_obj_clear_flag(start_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(start_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    return true;
}

void ui_voice_pair_set_status(const char *status, const char *expiry) {
    if (!root) return;
    lv_label_set_text(status_label, status ? status : "");
    lv_label_set_text(expiry_label, expiry ? expiry : "");
}

void ui_voice_pair_finish(const char *status) {
    if (!root) return;
    wipe_code();
    lv_label_set_text(status_label, status ? status : "");
    lv_label_set_text(expiry_label, "");
    if (start_button) lv_obj_add_flag(start_button, LV_OBJ_FLAG_HIDDEN);
}

void ui_voice_pair_error(const char *status) {
    if (!root) return;
    wipe_code();
    pairing_started = false;
    lv_label_set_text(status_label, status ? status : "");
    lv_label_set_text(expiry_label, "");
    if (start_button) lv_obj_clear_flag(start_button, LV_OBJ_FLAG_HIDDEN);
}

void ui_voice_pair_hide(void) {
    wipe_code();
    if (root) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    pairing_started = false;
    if (start_button) lv_obj_clear_flag(start_button, LV_OBJ_FLAG_HIDDEN);
    if (status_label) lv_label_set_text(status_label, "");
    if (expiry_label) lv_label_set_text(expiry_label, "");
}

bool ui_voice_pair_is_visible(void) { return root && !lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN); }

#if defined(SIMULATOR)
const char *ui_voice_pair_sim_code_text(void) { return code_label ? lv_label_get_text(code_label) : ""; }
bool ui_voice_pair_sim_has_qr(void) { return qr != nullptr; }
bool ui_voice_pair_sim_start_visible(void) { return start_button && !lv_obj_has_flag(start_button, LV_OBJ_FLAG_HIDDEN); }
bool ui_voice_pair_sim_is_visible(void) { return ui_voice_pair_is_visible(); }
#endif
