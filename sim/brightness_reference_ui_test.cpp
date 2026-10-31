#define HOMEDASH_BRIGHTNESS_REFERENCE_TEST 1
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>

static brightness_reference_status_t reference = {BRIGHTNESS_REFERENCE_IDLE, 0};
static int reference_writes;
brightness_reference_status_t brightness_runtime_reference_status() { return reference; }
bool brightness_runtime_reference_baseline() { ++reference_writes; reference.phase = BRIGHTNESS_REFERENCE_CONFIRM; return true; }
bool brightness_runtime_reference_preview() { ++reference_writes; reference = {BRIGHTNESS_REFERENCE_PREVIEW, 20}; return true; }
static void fits(lv_obj_t *label) {
    lv_point_t measured = {};
    lv_text_get_size(&measured, lv_label_get_text(label), lv_obj_get_style_text_font(label, 0),
        lv_obj_get_style_text_letter_space(label, 0), lv_obj_get_style_text_line_space(label, 0),
        lv_obj_get_width(label), LV_TEXT_FLAG_NONE);
    assert(measured.y <= lv_obj_get_height(label));
}
int main() {
    lv_init(); lv_display_create(1024, 600); ui_dashboard_create(); ui_dashboard_open_settings();
    assert(reference_writes == 0 && settings_reference_status && settings_reference_label);
    reference_test_cb(nullptr);
    assert(reference.phase == BRIGHTNESS_REFERENCE_CONFIRM && reference_writes == 1);
    reference_test_cb(nullptr);
    assert(reference.phase == BRIGHTNESS_REFERENCE_PREVIEW && reference_writes == 2);
    for (const char *locale : {"ru", "en", "sr-Latn"}) {
        ui_strings_set_locale(locale);
        for (auto phase : {BRIGHTNESS_REFERENCE_IDLE, BRIGHTNESS_REFERENCE_CONFIRM,
                BRIGHTNESS_REFERENCE_PREVIEW, BRIGHTNESS_REFERENCE_RESTORED,
                BRIGHTNESS_REFERENCE_ERROR, BRIGHTNESS_REFERENCE_DISABLED}) {
            reference = {phase, 12}; refresh_reference_test(); lv_obj_update_layout(lv_screen_active());
            fits(settings_reference_status); fits(settings_reference_label);
            assert(lv_label_get_text(settings_reference_status)[0]);
            if (phase == BRIGHTNESS_REFERENCE_PREVIEW) assert(strstr(lv_label_get_text(settings_reference_status), "12"));
        }
    }
    puts("brightness reference UI: two explicit steps, countdown, error/restore states and RU/EN/sr-Latn layout passed");
}
