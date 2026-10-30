// Exercise actual LVGL navigation and recovery privacy with isolated fixtures.
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>
#include <cstring>

static int64_t now_us = 1000000;
static int starts = 0, stops = 0;
extern "C" int64_t esp_timer_get_time() { return now_us; }
void request_recovery_ap() { ++starts; }
void request_stop_recovery_ap() { ++stops; }
static void masked_at(int line) {
    if (settings_reveal_until_us) std::fprintf(stderr, "Reveal remained active at test line %d\n", line);
    assert(settings_reveal_until_us == 0);
    assert(!strcmp(lv_label_get_text(settings_password), "****************"));
}
#define masked() masked_at(__LINE__)
static bool contains(lv_obj_t *obj, const char *text) {
    if (lv_obj_check_type(obj, &lv_label_class) && strstr(lv_label_get_text(obj), text)) return true;
    for (uint32_t i=0; i<lv_obj_get_child_count(obj); ++i)
        if (contains(lv_obj_get_child(obj, i), text)) return true;
    return false;
}
int main() {
    lv_init(); lv_display_create(1024,600); ui_dashboard_create();
    lv_obj_update_layout(lv_screen_active());
    int bindings = 0;
    for (const auto &binding : static_ui_labels) if (binding.label) ++bindings;
    assert(bindings < MAX_STATIC_UI_LABELS);
    ui_dashboard_open_settings();
    assert(settings_is_active() && starts == 0 && stops == 0);
    recovery_toggle_cb(nullptr); assert(starts == 1);
    ui_dashboard_update_recovery("HomeDash-Setup-DEMO", "TestOnlyPassword", "http://192.168.4.1");
    masked();
    assert(!contains(nav_tiles[2], "TestOnlyPassword"));
    assert(!contains(nav_tiles[2], "HomeDash-Setup-DEMO"));
    reveal_password_cb(nullptr);
    assert(!strcmp(lv_label_get_text(settings_password), "TestOnlyPassword"));
    now_us += 14999000; settings_privacy_tick(nullptr); assert(settings_reveal_until_us);
    now_us += 1000; settings_privacy_tick(nullptr); masked();
    reveal_password_cb(nullptr); lv_obj_send_event(tileview, LV_EVENT_SCROLL_BEGIN, nullptr); masked();
    reveal_password_cb(nullptr); ui_dashboard_sim_set_page(2); settings_privacy_tick(nullptr); masked();
    assert(stops == 0); // Leaving does not interrupt phone setup.
    ui_dashboard_open_settings(); reveal_password_cb(nullptr); set_screen_off(true); masked(); set_screen_off(false);
    web_preferences_t prefs = {}; web_preferences_defaults(&prefs); prefs.revision = 1;
    snprintf(prefs.settings_url, sizeof(prefs.settings_url), "%s", "https://homedash.example/settings/?device_id=0123456789abcdef0123456789abcdef");
    for (const char *locale : {"ru", "en", "sr-Latn"}) {
        ui_dashboard_open_settings(); reveal_password_cb(nullptr);
        snprintf(prefs.locale, sizeof(prefs.locale), "%s", locale);
        ui_dashboard_apply_preferences(&prefs); masked();
        assert(!strcmp(settings_qr_url, prefs.settings_url));
        assert(!lv_obj_has_flag(settings_qr, LV_OBJ_FLAG_HIDDEN));
        lv_obj_update_layout(lv_screen_active());
        assert(ui_dashboard_sim_fixed_labels_fit());
    }
    prefs.visible_home = false; ui_dashboard_apply_preferences(&prefs); ui_dashboard_open_settings();
    assert(settings_is_active());
    for (const char *url : {"https://user:secret@example/settings/", "https://example/settings/?token=secret", "https://example/settings/#secret", "https://example/settings/?device_id=bad"})
        assert(!public_settings_url(url));
    assert(public_settings_url("http://192.168.1.2:8080/settings/"));
    snprintf(prefs.settings_url, sizeof(prefs.settings_url), "%s", "https://example/settings/?token=secret");
    ui_dashboard_apply_preferences(&prefs);
    assert(lv_obj_has_flag(settings_qr, LV_OBJ_FLAG_HIDDEN));
    assert(!contains(nav_tiles[3], "token=secret"));
    reveal_password_cb(nullptr); recovery_toggle_cb(nullptr); masked(); assert(stops == 1);
    ui_dashboard_update_recovery(nullptr,nullptr,nullptr); masked(); assert(!ui_recovery_password[0]);
    reveal_password_cb(nullptr); masked();
    ui_dashboard_update_photo_status("Авто: после простоя без касаний");
    assert(!lv_label_get_text(home_photo_status)[0]);
    bridge_data_t data = {}; data.sensors_valid = true; data.sensor_count = 4;
    const char *values[] = {"unavailable", "unknown", "NaN", ""};
    for (int i=0;i<4;++i) {
        snprintf(data.sensors[i].name, sizeof(data.sensors[i].name), "%s", "Very long air quality sensor friendly name for wrapping");
        snprintf(data.sensors[i].value, sizeof(data.sensors[i].value), "%s", values[i]);
        snprintf(data.sensors[i].unit, sizeof(data.sensors[i].unit), "%s", "ppm");
    }
    ui_dashboard_update_ha(&data); lv_obj_update_layout(lv_screen_active());
    for (int i=0;i<4;++i) {
        assert(!strcmp(lv_label_get_text(home_air_values[i]), "—"));
        assert(lv_color_eq(lv_obj_get_style_text_color(home_air_values[i],0),COLOR_TEXT_DIM));
        assert(lv_obj_get_width(home_air_names[i]) == 174);
        assert(lv_obj_get_height(home_air_names[i]) == 44);
        assert(lv_obj_get_y(home_air_names[i]) >= lv_obj_get_y(home_air_values[i]) + lv_obj_get_height(home_air_values[i]));
    }
    puts("PASS: four tabs, explicit recovery controls, timed password privacy, locale/screen/swipe lifecycle, public QR and unavailable sensors");
}
