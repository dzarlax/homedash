#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <vector>
#include "action_feedback.h"

#define BRIDGE_MAX_TASKS   8
#define BRIDGE_MAX_NEWS    5
#define BRIDGE_MAX_SENSORS 10
#define BRIDGE_MAX_LIGHTS  8
#define BRIDGE_MAX_CLIMATES 1

#define BRIDGE_FORECAST_DAYS      5
#define BRIDGE_TRANSPORT_STOPS    2
#define BRIDGE_TRANSPORT_VEHICLES 5

// Freshness is independent of payload validity: a failed request keeps last good data.
struct bridge_section_t {
 bool known;
 bool enabled;
 bool last_attempt_failed;
 bool has_received;
 bool source_age_known;
 uint32_t source_age_seconds;
 int64_t received_ms;
};
struct bridge_health_t {
 uint16_t present;
 bool presence_known;
    int  steps;
    int  steps_prev;
    int  cal;
    int  cal_prev;
    float sleep;
    float sleep_prev;
    int  hr;
    int  rhr;
    int  hrv;
    int  spo2;
    int  readiness;
    bool valid;
};

struct bridge_task_t {
    char title[160];
    int  priority;
    char due[12];   // "2026-03-29"
};

struct bridge_news_t {
    char title[256];
    char summary[1536];
    char category[24];
    int  hours_ago;
};

struct bridge_sensor_t {
    char name[40];
    char value[16];
    char unit[8];
};

struct bridge_light_t {
    char entity_id[48];
    char name[40];
    bool on;
    int  brightness;   // 0-255
};

struct bridge_climate_t {
    char  entity_id[48];
    char  name[40];
    char  mode[16];
    float current_temp;
    float target_temp;
    bool  has_current_temp;
    bool  has_target_temp;
};

// Weather
struct bridge_weather_daily_t {
    float temp_max;
    float temp_min;
    int   weather_code;
};

struct bridge_weather_t {
    float temp;
    float humidity;
    float wind_speed;
    int   weather_code;
    bridge_weather_daily_t daily[BRIDGE_FORECAST_DAYS];
    int   daily_count;
    bool  valid;
    int64_t received_ms; // Local monotonic receipt time, never a provider timestamp.
    bool last_attempt_failed;
};

// Transport
struct bridge_transport_vehicle_t {
    char line_number[8];
    int  seconds_left;
    int  stations_between;
};

struct bridge_transport_stop_t {
    bridge_transport_vehicle_t vehicles[BRIDGE_TRANSPORT_VEHICLES];
    int count;
};

struct bridge_transport_t {
    bridge_transport_stop_t stops[BRIDGE_TRANSPORT_STOPS];
    bool valid;
    int64_t received_ms;
    bool last_attempt_failed;
};

// Calendar
struct bridge_cal_event_t {
    char    summary[64];
    uint8_t start_hour;
    uint8_t start_min;
    uint8_t end_hour;
    uint8_t end_min;
    bool    all_day;
    uint8_t cal_idx;
};

struct bridge_cal_data_t {
    std::vector<bridge_cal_event_t> events;
    int   count;
    bool  valid;
    int   year, month, day;
    int64_t received_ms;
    bool last_attempt_failed;
    bool completeness_known;
    bool sources_partial;
    bool cache_stale;
};

struct bridge_data_t {
    uint32_t ts;
    bridge_section_t health_state, tasks_state, news_state;

    bridge_health_t health;

    bridge_task_t tasks[BRIDGE_MAX_TASKS];
    int task_count;
    bool tasks_valid;

    bridge_news_t news[BRIDGE_MAX_NEWS];
    int news_count;
    bool news_valid;

    bridge_sensor_t sensors[BRIDGE_MAX_SENSORS];
    int sensor_count;
    bool sensors_valid;

    bridge_light_t lights[BRIDGE_MAX_LIGHTS];
    int light_count;
    bool lights_valid;

    bridge_climate_t climates[BRIDGE_MAX_CLIMATES];
    int climate_count;
    bool climate_valid;

    bridge_weather_t   weather;
    bridge_transport_t transport;
};

void bridge_init(const char *url, const char *api_key);
void bridge_fetch_and_update(void);
void bridge_toggle_light(const char *entity_id);
void bridge_climate_action(const char *entity_id, const char *action, float temperature, const char *hvac_mode);
void bridge_fetch_calendar(int year, int month, int day);
void bridge_refresh_calendar_if_due(void);
bool bridge_copy_data(bridge_data_t *out);
bool bridge_has_data(void);
bool bridge_copy_calendar_data(bridge_cal_data_t *out);
bool bridge_copy_last_error(char *out, size_t out_size);
const char *bridge_get_url(void);
const char *bridge_get_api_key(void);

struct bridge_action_result_t {
    action_phase_t phase;
    action_error_t error;
    bool has_observation;
    action_observation_t observation;
};
bridge_action_result_t bridge_action_request(const char *entity, const char *action, float temperature, const char *mode);
bridge_action_result_t bridge_action_read(const char *entity, int timeout_ms = 3000);
// Caller checks the active request ID under LVGL lock before merging a read.
void bridge_action_merge_observation(const action_observation_t &observation);
