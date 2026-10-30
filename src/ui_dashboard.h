#pragma once

#include "bridge.h"
#include "web_settings.h"
#include "lvgl.h"

void ui_dashboard_create(void);
// Call only while holding the LVGL lock.
void ui_dashboard_open_settings(void);
void ui_dashboard_hide_sensitive(void);
void ui_dashboard_update_weather(const bridge_weather_t *data);
void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data);
void ui_dashboard_update_transport(const bridge_transport_t *data);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);
void ui_dashboard_update_time(void);

bool ui_dashboard_reader_open(void);
bool ui_dashboard_apply_brightness(const web_preferences_t *prefs);
void ui_dashboard_apply_preferences(const web_preferences_t *prefs);
void ui_dashboard_reset_preferences(void);
void ui_dashboard_update_recovery(const char *ssid, const char *password, const char *url);
bool ui_dashboard_filter_wake_touch(bool pressed, lv_indev_data_t *data);
#if defined(SIMULATOR)
void ui_dashboard_sim_set_page(int page);
void ui_dashboard_sim_refresh_feedback(void);
void ui_dashboard_sim_set_selected_temperature(float value);
bool ui_dashboard_sim_fixed_labels_fit(void);
int ui_dashboard_sim_current_page(void);
void ui_dashboard_sim_open_reader(void);
void ui_dashboard_sim_close_reader(void);
int32_t ui_dashboard_sim_reader_scroll_y(void);
#endif

// Caller holds the LVGL lock.
void ui_dashboard_refresh_ha(void);

bool ui_dashboard_timezone_matches(const char *timezone, const char *posix_timezone);
