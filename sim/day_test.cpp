// Exercise Day UI state transitions using real LVGL objects.
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>

int main() {
    lv_init(); lv_display_create(1024, 600); ui_dashboard_create();
    bridge_weather_t weather = {}; weather.valid = true; weather.temp = 0; weather.weather_code = 0;
    ui_dashboard_update_weather(&weather);
    assert(!strcmp(lv_label_get_text(lbl_topbar_temp), "0°"));
    assert(!strcmp(lv_label_get_text(lbl_weather_detail), ""));
    assert(!lv_obj_has_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN));
    weather.last_attempt_failed = true; ui_dashboard_update_weather(&weather);
    assert(!strcmp(lv_label_get_text(lbl_topbar_temp), "0°"));
    assert(strstr(lv_label_get_text(lbl_weather_freshness), "Последние данные:") && strstr(lv_label_get_text(lbl_weather_freshness), "мин назад"));
    weather = {}; ui_dashboard_update_weather(&weather);
    assert(!strcmp(lv_label_get_text(lbl_topbar_temp), "--"));
    assert(lv_obj_has_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN));

    bridge_cal_data_t selected = {}; selected.valid = true;
    selected.year = 2099; selected.month = 1; selected.day = 15;
    selected.count = 1; selected.events.resize(1);
    selected.events[0] = {"Selected date event", 12, 0, 13, 0, false, 0};
    sel_year = selected.year; sel_month = selected.month; sel_day = selected.day;
    update_schedule_title(sel_year, sel_month, sel_day);
    ui_dashboard_update_ha_calendar(&selected);
    assert(!strcmp(lv_label_get_text(lbl_hero_title), "Selected date event"));
    bridge_cal_data_t late = selected; late.day = 14;
    snprintf(late.events[0].summary, sizeof(late.events[0].summary), "Old reply");
    ui_dashboard_update_ha_calendar(&late);
    assert(sel_day == 15 && !strcmp(lv_label_get_text(lbl_hero_title), "Selected date event"));
    sel_day = 16; update_schedule_title(sel_year, sel_month, sel_day);
    assert(strcmp(lv_label_get_text(lbl_hero_title), "Selected date event"));
    ui_dashboard_update_ha_calendar(&selected);
    assert(sel_day == 16 && strcmp(lv_label_get_text(lbl_hero_title), "Selected date event"));

    const char *locales[] = {"ru", "en", "sr-Latn"};
    for (const char *locale : locales) {
        web_preferences_t prefs = {}; web_preferences_defaults(&prefs); prefs.revision = 1;
        snprintf(prefs.settings_url, sizeof(prefs.settings_url), "https://example.test/settings/");
        snprintf(prefs.locale, sizeof(prefs.locale), "%s", locale);
        prefs.visible_weather = prefs.visible_transport = prefs.visible_home = false;
        ui_dashboard_apply_preferences(&prefs);
        assert(lv_obj_has_flag(day_weather_card, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(day_transport_card, LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(day_air_card, LV_OBJ_FLAG_HIDDEN));
        prefs.visible_weather = prefs.visible_transport = prefs.visible_home = true;
        ui_dashboard_apply_preferences(&prefs);
        assert(!lv_obj_has_flag(day_weather_card, LV_OBJ_FLAG_HIDDEN));
        weather.valid = true; weather.temp = -5; weather.daily_count = 1;
        weather.daily[0] = {2, -7, 0}; ui_dashboard_update_weather(&weather);
        assert(strstr(lv_label_get_text(lbl_weather_detail), "-7"));
        assert(strstr(lv_label_get_text(lbl_weather_detail), "2"));
        lv_obj_update_layout(lv_screen_active());
        assert(lv_obj_get_width(day_weather_card) == 326);
        assert(lv_obj_get_y(day_air_card) + lv_obj_get_height(day_air_card) <= 448);
    }
    bridge_cal_data_t partial = {}; partial.valid = true; partial.sources_partial = true;
    partial.completeness_known = true; partial.year = sel_year; partial.month = sel_month; partial.day = sel_day;
    ui_dashboard_update_ha_calendar(&partial);
    assert(!strcmp(lv_label_get_text(lbl_hero_title), ui_text("Список событий неполный")));
    assert(!strcmp(lv_label_get_text(lbl_day_context), ui_text("Неполное расписание")));
    partial.sources_partial = false; partial.completeness_known = false;
    ui_dashboard_update_ha_calendar(&partial);
    assert(!strcmp(lv_label_get_text(lbl_hero_title), ui_text("Нет событий в ответе")));
    ui_dashboard_photo_exit();
    assert(ui_last_touch == esp_timer_get_time() / 1000000);
    puts("PASS: Day zero/missing/stale weather, selected-date race, all locale visibility, compact geometry and photo exit");
}
