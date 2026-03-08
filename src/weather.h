#pragma once

#include <stdint.h>

#define WEATHER_FORECAST_DAYS 5

struct weather_daily_t {
    float temp_max;
    float temp_min;
    int   weather_code;
};

struct weather_data_t {
    float temp;
    float humidity;
    float wind_speed;
    int   weather_code;

    weather_daily_t daily[WEATHER_FORECAST_DAYS];

    bool  valid;
};

const char *weather_code_to_text(int code);
void weather_fetch_and_update(void);
const weather_data_t *weather_get_data(void);
const char *weather_get_last_error(void);
