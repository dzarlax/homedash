#pragma once

#include "bridge.h"
#include "web_settings.h"
#include "lvgl.h"

void ui_dashboard_create(void);
void ui_dashboard_update_weather(const bridge_weather_t *data);
void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data);
void ui_dashboard_update_transport(const bridge_transport_t *data);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);
void ui_dashboard_update_time(void);

bool ui_dashboard_reader_open(void);
void ui_dashboard_update_photo_status(const char *status);
void ui_dashboard_apply_preferences(const web_preferences_t *prefs);
void ui_dashboard_reset_preferences(void);
void ui_dashboard_update_recovery(const char *ssid, const char *password, const char *url);
bool ui_dashboard_filter_wake_touch(bool pressed, lv_indev_data_t *data);
#if defined(SIMULATOR)
void ui_dashboard_sim_set_page(int page);
bool ui_dashboard_sim_fixed_labels_fit(void);
int ui_dashboard_sim_current_page(void);
void ui_dashboard_sim_open_reader(void);
void ui_dashboard_sim_close_reader(void);
int32_t ui_dashboard_sim_reader_scroll_y(void);
#endif
