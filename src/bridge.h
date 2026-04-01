#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BRIDGE_MAX_TASKS   8
#define BRIDGE_MAX_NEWS    5
#define BRIDGE_MAX_SENSORS 10
#define BRIDGE_MAX_LIGHTS  8

#define BRIDGE_FORECAST_DAYS      5
#define BRIDGE_TRANSPORT_STOPS    2
#define BRIDGE_TRANSPORT_VEHICLES 5
#define BRIDGE_CAL_MAX_EVENTS     20

struct bridge_health_t {
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
    bridge_cal_event_t events[BRIDGE_CAL_MAX_EVENTS];
    int   count;
    bool  valid;
    int   year, month, day;
};

struct bridge_data_t {
    uint32_t ts;

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

    bridge_weather_t   weather;
    bridge_transport_t transport;
};

void bridge_init(const char *url, const char *api_key);
void bridge_fetch_and_update(void);
void bridge_toggle_light(const char *entity_id);
void bridge_fetch_calendar(int year, int month, int day);
const bridge_data_t *bridge_get_data(void);
const bridge_cal_data_t *bridge_get_calendar_data(void);
const char *bridge_get_last_error(void);
const char *bridge_get_url(void);
const char *bridge_get_api_key(void);
