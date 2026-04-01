#pragma once

#include "bridge.h"

void ui_dashboard_create(void);
void ui_dashboard_update_weather(const bridge_weather_t *data);
void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data);
void ui_dashboard_update_transport(const bridge_transport_t *data);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);
void ui_dashboard_update_time(void);
