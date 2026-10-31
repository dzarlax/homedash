#define HOMEDASH_BRIGHTNESS_REFERENCE_TEST 1
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>

static brightness_reference_status_t reference = {BRIGHTNESS_REFERENCE_IDLE, 0};
static int reference_writes;
brightness_reference_status_t brightness_runtime_reference_status() { return reference; }
bool brightness_runtime_reference_baseline() { ++reference_writes; reference.phase = BRIGHTNESS_REFERENCE_CONFIRM; return true; }
bool brightness_runtime_reference_preview(uint8_t command) { ++reference_writes; reference = {BRIGHTNESS_REFERENCE_PREVIEW, 20, command}; return true; }
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
    assert(reference.phase == BRIGHTNESS_REFERENCE_PREVIEW && reference_writes == 2 && reference.test_command == 40);
    assert(lv_obj_has_flag(settings_reference_night_button, LV_OBJ_FLAG_HIDDEN));
    reference_night_cb(nullptr); assert(reference_writes == 2);
    reference.phase = BRIGHTNESS_REFERENCE_RESTORED; refresh_reference_test();
    reference_night_cb(nullptr);
    assert(reference.phase == BRIGHTNESS_REFERENCE_PREVIEW && reference_writes == 3 && reference.test_command == 70);
    for (const char *locale : {"ru", "en", "sr-Latn"}) {
        ui_strings_set_locale(locale);
        for (auto phase : {BRIGHTNESS_REFERENCE_IDLE, BRIGHTNESS_REFERENCE_CONFIRM,
                BRIGHTNESS_REFERENCE_PREVIEW, BRIGHTNESS_REFERENCE_RESTORED,
                BRIGHTNESS_REFERENCE_ERROR, BRIGHTNESS_REFERENCE_DISABLED}) {
            reference = {phase, 12, 70}; refresh_reference_test(); lv_obj_update_layout(lv_screen_active());
            fits(settings_reference_status); fits(settings_reference_label);
            fits(lv_obj_get_child(settings_reference_night_button, 0));
            if (phase == BRIGHTNESS_REFERENCE_CONFIRM || phase == BRIGHTNESS_REFERENCE_RESTORED) {
                assert(!lv_obj_has_flag(settings_reference_night_button, LV_OBJ_FLAG_HIDDEN));
                assert(lv_obj_get_y(settings_reference_button) + lv_obj_get_height(settings_reference_button) < lv_obj_get_y(settings_reference_night_button));
            } else assert(lv_obj_has_flag(settings_reference_night_button, LV_OBJ_FLAG_HIDDEN));
            assert(lv_label_get_text(settings_reference_status)[0]);
            if (phase == BRIGHTNESS_REFERENCE_PREVIEW) {
                assert(strstr(lv_label_get_text(settings_reference_status), "12"));
                reference.test_command = 40; refresh_reference_test(); lv_obj_update_layout(lv_screen_active());
                fits(settings_reference_status);
            }
        }
    }
    puts("brightness reference UI: reference gate, brighter/night choices, no overlapping buttons, countdown, error/restore states and RU/EN/sr-Latn layout passed");
}
