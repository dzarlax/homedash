// Exercise the real dashboard mapping with bounded Bridge fixtures.
#include "../src/ui_dashboard.cpp"
#include <cassert>
#include <string>

static std::string toggled_entity;

uint32_t request_home_action(const char *entity_id, const char *action, float, const char *)
{
    toggled_entity = entity_id ? entity_id : "";
    assert(strcmp(action, "turn_off") == 0);
    return 1;
}

static void put_light(bridge_data_t &data, int index, const char *id, const char *name, bool on)
{
    std::snprintf(data.lights[index].entity_id, sizeof(data.lights[index].entity_id), "%s", id);
    std::snprintf(data.lights[index].name, sizeof(data.lights[index].name), "%s", name);
    data.lights[index].on = on;
}

static void put_sensor(bridge_data_t &data, int index, const char *name, const char *value, const char *unit)
{
    std::snprintf(data.sensors[index].name, sizeof(data.sensors[index].name), "%s", name);
    std::snprintf(data.sensors[index].value, sizeof(data.sensors[index].value), "%s", value);
    std::snprintf(data.sensors[index].unit, sizeof(data.sensors[index].unit), "%s", unit);
}

int main()
{
    lv_init();
    lv_display_create(1024, 600);
    ui_dashboard_create();
    lv_obj_update_layout(lv_screen_active());

    bridge_data_t data = {};
    data.lights_valid = true;
    data.light_count = 7;
    put_light(data, 0, "light.svet_u_divana", "Диван", true);
    put_light(data, 1, "light.svet_u_okna", "Окно", false);
    put_light(data, 2, "light.office_light", "Кабинет", true);
    put_light(data, 3, "light.bedroom_light", "Спальня", false);
    put_light(data, 4, "light.yeelink_bslamp2_2272_light", "Лампа", true);
    put_light(data, 5, "light.kukhnia", "Кухня", true);
    put_light(data, 6, "light.new_selected_lamp", "Новая лампа", true);
    data.sensors_valid = true;
    data.sensor_count = 4;
    put_sensor(data, 0, "CO2 гостиная", "612", "ppm");
    put_sensor(data, 1, "CO2 кабинет", "824", "ppm");
    put_sensor(data, 2, "PM2.5", "4", "ug/m3");
    put_sensor(data, 3, "Влажность", "44", "%");
    ui_dashboard_update_ha(&data);

    assert(std::strcmp(room_light_entity_ids[0][0], "light.svet_u_divana") == 0);
    assert(std::strcmp(lv_label_get_text(room_light_labels[0][0]), "Диван") == 0);
    assert(std::strcmp(room_light_entity_ids[0][1], "light.svet_u_okna") == 0);
    assert(std::strcmp(room_light_entity_ids[1][0], "light.office_light") == 0);
    assert(std::strcmp(room_light_entity_ids[2][0], "light.bedroom_light") == 0);
    assert(std::strcmp(room_light_entity_ids[2][1], "light.yeelink_bslamp2_2272_light") == 0);
    assert(std::strcmp(room_light_entity_ids[3][0], "light.kukhnia") == 0);
    assert(std::strcmp(room_light_entity_ids[0][2], "light.new_selected_lamp") == 0);
    assert(std::strcmp(lv_label_get_text(room_light_labels[0][2]), "Новая лампа") == 0);
    lv_obj_send_event(room_light_btns[0][2], LV_EVENT_CLICKED, NULL);
    assert(toggled_entity == "light.new_selected_lamp");
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Гостиная CO2") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[1]), "Кабинет CO2") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[2]), "PM2.5") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_values[3]), "44 %") == 0);

    web_preferences_t locale_prefs = {};
    web_preferences_defaults(&locale_prefs);
    locale_prefs.revision = 1;
    std::snprintf(locale_prefs.settings_url, sizeof(locale_prefs.settings_url), "%s", "http://homedash.local");
    std::snprintf(locale_prefs.locale, sizeof(locale_prefs.locale), "%s", "en");
    ui_dashboard_apply_preferences(&locale_prefs);
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Living room CO2") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[1]), "Study CO2") == 0);
    std::snprintf(locale_prefs.locale, sizeof(locale_prefs.locale), "%s", "sr-Latn");
    ui_dashboard_apply_preferences(&locale_prefs);
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "CO2 u dnevnoj sobi") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[1]), "CO2 u radnoj sobi") == 0);
    std::snprintf(locale_prefs.locale, sizeof(locale_prefs.locale), "%s", "ru");
    ui_dashboard_apply_preferences(&locale_prefs);
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Гостиная CO2") == 0);

    for (int i = 7; i < BRIDGE_MAX_LIGHTS; i++) {
        char id[48], name[40];
        std::snprintf(id, sizeof(id), "light.extra_%d", i);
        std::snprintf(name, sizeof(name), "Extra lamp %d", i);
        put_light(data, i, id, name, false);
    }
    data.light_count = BRIDGE_MAX_LIGHTS + 2; // Clamp untrusted counts to the bridge array.
    ui_dashboard_update_ha(&data);
    int mapped_lights = 0;
    for (int r = 0; r < MAX_ROOMS; r++) {
        for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
            if (room_light_entity_ids[r][l][0]) mapped_lights++;
        }
    }
    assert(mapped_lights == BRIDGE_MAX_LIGHTS);

    // Newly selected entities show their friendly names and values. Missing
    // previous entries clear their old labels and values on every refresh.
    data.light_count = 1;
    put_light(data, 0, "light.other_selected_lamp", "Гаражная лампа", false);
    data.sensor_count = 2;
    put_sensor(data, 0, "Garage air sensor", "18", "ppb");
    put_sensor(data, 1, "Влажность кухни", "51", "%");
    ui_dashboard_update_ha(&data);
    assert(std::strcmp(room_light_entity_ids[0][0], "light.other_selected_lamp") == 0);
    assert(std::strcmp(lv_label_get_text(room_light_labels[0][0]), "Гаражная лампа") == 0);
    assert(lv_obj_has_flag(room_light_btns[0][1], LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(room_light_btns[0][2], LV_OBJ_FLAG_HIDDEN));
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Garage air sensor") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_values[0]), "18 ppb") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[1]), "Влажность кухни") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_values[1]), "51 %") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_values[2]), "---") == 0);
    std::snprintf(locale_prefs.locale, sizeof(locale_prefs.locale), "%s", "en");
    ui_dashboard_apply_preferences(&locale_prefs);
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Garage air sensor") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[1]), "Влажность кухни") == 0);
    std::snprintf(locale_prefs.locale, sizeof(locale_prefs.locale), "%s", "sr-Latn");
    ui_dashboard_apply_preferences(&locale_prefs);
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Garage air sensor") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_names[1]), "Влажность кухни") == 0);
    std::snprintf(locale_prefs.locale, sizeof(locale_prefs.locale), "%s", "ru");
    ui_dashboard_apply_preferences(&locale_prefs);

    data.light_count = 0;
    data.sensor_count = 0;
    ui_dashboard_update_ha(&data);
    for (int r = 0; r < MAX_ROOMS; r++) {
        for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
            assert(room_light_entity_ids[r][l][0] == '\0');
            assert(lv_obj_has_flag(room_light_btns[r][l], LV_OBJ_FLAG_HIDDEN));
        }
    }
    assert(std::strcmp(lv_label_get_text(home_air_names[0]), "Гостиная CO2") == 0);
    assert(std::strcmp(lv_label_get_text(home_air_values[0]), "---") == 0);

    data.light_count = BRIDGE_MAX_LIGHTS;
    data.lights_valid = false;
    ui_dashboard_update_ha(&data);
    for (int r = 0; r < MAX_ROOMS; r++) {
        for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
            assert(room_light_entity_ids[r][l][0] == '\0');
            assert(lv_obj_has_flag(room_light_btns[r][l], LV_OBJ_FLAG_HIDDEN));
        }
    }

    std::puts("PASS: known and fallback lights, mapped callbacks, dynamic sensors, and stale-list clearing");
}
