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
