#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <cstdio>
#include <string>
void sim_feedback_reset();
void sim_feedback_advance(unsigned);
void sim_feedback_next_error(action_error_t);
void sim_feedback_wifi(bool);
unsigned sim_feedback_count();
void sim_action_result(uint32_t, action_phase_t, action_error_t);
int main() {
    lv_init(); lv_display_create(1024,600); sim_feedback_reset(); ui_dashboard_create();
    bridge_data_t data = {}; data.lights_valid=true; data.light_count=2;
    data.lights[0]={"light.svet_u_divana","Sofa",false,0}; data.lights[1]={"light.svet_u_okna","Window",true,0};
    data.climate_valid=true; data.climate_count=1; data.climates[0]={"climate.gostinaia","AC","cool",24,22,true,true};
    ui_bridge_snapshot = data; ui_dashboard_update_ha(&data);
    web_preferences_t favs = {}; web_preferences_defaults(&favs); favs.revision=1;
    snprintf(favs.settings_url,sizeof(favs.settings_url),"https://example.test");
    favs.favorite_count=3;
    favs.favorites[0]={"light.svet_u_divana","light","My light"};
    favs.favorites[1]={"light.missing","light","Missing"};
    favs.favorites[2]={"climate.gostinaia","climate","Climate"};
    struct tm night = {}; night.tm_hour = 23; favs.night_enabled=true;
    assert(clock_is_night_for(favs, night)); night.tm_hour=7; assert(!clock_is_night_for(favs, night));
    night.tm_hour=2; night.tm_isdst=0; assert(clock_is_night_for(favs, night));
    night.tm_isdst=1; assert(clock_is_night_for(favs, night));
    const char *old_tz = getenv("TZ"); std::string saved_tz = old_tz ? old_tz : ""; bool had_tz = old_tz != nullptr;
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
    strcpy(favs.night_start,"01:30"); strcpy(favs.night_end,"03:00");
    time_t boundary[] = {1774745940, 1774746000, 1792889940, 1792890000};
    bool expected[] = {true, false, true, true};
    for (int i=0; i<4; ++i) { struct tm local = {}; localtime_r(&boundary[i], &local); assert(clock_is_night_for(favs, local) == expected[i]); }
    if (had_tz) setenv("TZ", saved_tz.c_str(), 1); else unsetenv("TZ"); tzset();
    strcpy(favs.night_start,"23:00"); strcpy(favs.night_end,"07:00"); favs.night_enabled=false;
    assert(web_preferences_valid(&favs)); ui_dashboard_apply_preferences(&favs); lv_obj_update_layout(lv_screen_active()); home_feedback_tick(nullptr);
    assert(!strcmp(lv_label_get_text(favorite_labels[0]),"My light"));
    assert(favorite_available[0] && !favorite_available[1] && favorite_available[2]);
    assert(!strcmp(lv_label_get_text(favorite_feedback[0]), "Включено") || !strcmp(lv_label_get_text(favorite_feedback[0]), "Выключено"));
    assert(!strcmp(lv_label_get_text(favorite_feedback[2]), "Открыть управление"));
    assert(lv_obj_has_state(favorite_buttons[1], LV_STATE_DISABLED));
    unsigned count_before=sim_feedback_count();
    lv_obj_send_event(favorite_buttons[1],LV_EVENT_CLICKED,nullptr);
    lv_obj_send_event(favorite_buttons[2],LV_EVENT_CLICKED,nullptr);
    assert(sim_feedback_count()==count_before);
    lv_obj_send_event(favorite_buttons[0],LV_EVENT_CLICKED,nullptr);
    home_feedback_tick(nullptr);
    assert(lv_obj_has_state(room_light_btns[0][0],LV_STATE_DISABLED));
    assert(lv_obj_has_state(favorite_buttons[0],LV_STATE_DISABLED));
    // A favorite and room refer to the same tracker entry.
    assert(home_action_find("light.svet_u_divana"));
    sim_feedback_reset(); ui_dashboard_update_ha(&data); home_feedback_tick(nullptr);
    lv_obj_send_event(room_light_btns[0][0],LV_EVENT_CLICKED,nullptr);
    assert(lv_obj_has_state(room_light_btns[0][0],LV_STATE_DISABLED)); assert(!lv_obj_has_state(room_light_btns[0][1],LV_STATE_DISABLED));
    const action_record_t *record=home_action_find("light.svet_u_divana"); assert(record && record->phase==ACTION_PENDING); uint32_t id=record->id;
    web_preferences_t prefs={}; web_preferences_defaults(&prefs); prefs.revision=1; snprintf(prefs.settings_url,sizeof(prefs.settings_url),"https://example.test"); snprintf(prefs.locale,sizeof(prefs.locale),"%s","en"); ui_dashboard_apply_preferences(&prefs); ui_dashboard_update_ha(&data); home_feedback_tick(nullptr);
    ui_dashboard_sim_set_page(3); snprintf(prefs.locale,sizeof(prefs.locale),"%s","sr-Latn"); ui_dashboard_apply_preferences(&prefs); home_feedback_tick(nullptr); assert(home_action_find("light.svet_u_divana")->id==id); assert(!strcmp(lv_label_get_text(room_light_feedback[0][0]),"Slanje..."));
    snprintf(prefs.locale,sizeof(prefs.locale),"%s","en"); ui_dashboard_apply_preferences(&prefs); ui_dashboard_update_ha(&data); ui_dashboard_sim_set_page(2); home_feedback_tick(nullptr);
    assert(home_action_find("light.svet_u_divana")->id==id); assert(!strcmp(lv_label_get_text(room_light_feedback[0][0]),"Sending..."));
    sim_feedback_next_error(ACTION_ERROR_QUEUE_FULL); lv_obj_send_event(room_light_btns[0][1],LV_EVENT_CLICKED,nullptr);
    assert(!strcmp(lv_label_get_text(room_light_feedback[0][1]),"Queue busy. Not sent")); assert(!lv_obj_has_state(room_light_btns[0][1],LV_STATE_DISABLED));
    std::string current_before = lv_label_get_text(home_climate_current); sim_feedback_wifi(false); lv_obj_send_event(home_climate_temp_buttons[1],LV_EVENT_CLICKED,nullptr);
    assert(!strcmp(lv_label_get_text(home_climate_feedback),"No Wi-Fi. Not sent")); assert(home_climate_target_value==22); assert(current_before == lv_label_get_text(home_climate_current));
    sim_feedback_wifi(true); std::string target_before = lv_label_get_text(home_climate_target); unsigned before=sim_feedback_count(); lv_obj_send_event(home_climate_temp_buttons[1],LV_EVENT_CLICKED,nullptr); sim_feedback_advance(300); lv_obj_send_event(home_climate_temp_buttons[1],LV_EVENT_CLICKED,nullptr);
    assert(sim_feedback_count()==before); assert(!strcmp(lv_label_get_text(home_climate_selected),"Selected 24 C")); assert(target_before == lv_label_get_text(home_climate_target));
    sim_feedback_advance(499); home_feedback_tick(nullptr); assert(sim_feedback_count()==before); sim_feedback_advance(1); home_feedback_tick(nullptr); assert(sim_feedback_count()==before+1); assert(lv_obj_has_state(home_climate_temp_buttons[0],LV_STATE_DISABLED));
    record=home_action_find("climate.gostinaia"); assert(record && record->target==24); sim_action_result(record->id,ACTION_REJECTED,ACTION_ERROR_UNAUTHORIZED); home_feedback_tick(nullptr);
    assert(home_climate_target_value==22); assert(!strcmp(lv_label_get_text(home_climate_selected),"")); assert(!strcmp(lv_label_get_text(home_climate_feedback),"No access. Rejected"));
    // An unconfirmed target must not become the base for the next +/- intent.
    lv_obj_send_event(home_climate_temp_buttons[1],LV_EVENT_CLICKED,nullptr);
    sim_feedback_advance(500); home_feedback_tick(nullptr);
    record=home_action_find("climate.gostinaia"); assert(record && record->target==23);
    sim_action_result(record->id,ACTION_UNKNOWN,ACTION_ERROR_TIMEOUT);
    sim_feedback_advance(20001); home_feedback_tick(nullptr);
    assert(home_climate_target_value==22);
    lv_obj_send_event(home_climate_temp_buttons[1],LV_EVENT_CLICKED,nullptr);
    assert(home_climate_target_value==23);
    puts("PASS: LVGL feedback, entity blocking, safe reasons, debounce, rollback and locale continuity");
}
