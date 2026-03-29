#pragma once

#include "weather.h"
#include "ha_calendar.h"
#include "transport.h"
#include "bridge.h"

void ui_dashboard_create(void);
void ui_dashboard_update_weather(const weather_data_t *data);
void ui_dashboard_update_ha_calendar(const ha_cal_data_t *data);
void ui_dashboard_update_transport(const transport_data_t *data);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);
void ui_dashboard_update_time(void);
