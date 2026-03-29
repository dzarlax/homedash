// Stubs for ESP-IDF and hardware functions used by UI code
#include <cstdio>
#include <cstring>
#include <cstdlib>

// --- weather stubs ---
#include "weather.h"
static weather_data_t s_weather = {
    .temp = 12.0, .humidity = 65, .wind_speed = 15,
    .weather_code = 1, .valid = true,
    .daily = {{.temp_max = 15, .temp_min = 5}, {.temp_max = 14, .temp_min = 4}}
};
const weather_data_t *weather_get_data() { return &s_weather; }
const char *weather_get_last_error() { return "OK"; }
const char *weather_code_to_text(int) { return "Cloudy"; }

// --- ha_calendar stubs ---
#include "ha_calendar.h"
static ha_cal_data_t s_cal = {
    .count = 3, .valid = true, .year = 2026, .month = 3, .day = 29,
    .events = {
        {.summary = "Team standup", .start_hour = 10, .start_min = 0, .end_hour = 10, .end_min = 30, .cal_idx = 0},
        {.summary = "Lunch with Maria", .start_hour = 13, .start_min = 0, .end_hour = 14, .end_min = 0, .cal_idx = 1},
        {.summary = "Dentist appointment", .start_hour = 16, .start_min = 30, .end_hour = 17, .end_min = 0, .cal_idx = 2},
    }
};
const ha_cal_data_t *ha_calendar_get_data() { return &s_cal; }
const char *ha_calendar_get_last_error() { return "OK"; }

// --- transport stubs ---
#include "transport.h"
static transport_data_t s_transport = {.valid = true};
const transport_data_t *transport_get_data() { return &s_transport; }
const char *transport_get_last_error() { return "OK"; }

// --- bridge stubs ---
#include "bridge.h"
const bridge_data_t *bridge_get_data() {
    static bridge_data_t d = {};
    return &d;
}
const char *bridge_get_last_error() { return "OK"; }

// --- wifi stubs ---
bool wifi_is_connected() { return true; }

// --- weather_icons stub ---
#include "lvgl.h"
void weather_icon_draw(lv_obj_t *, int) {}

// --- esp system stubs ---
extern "C" {
uint32_t esp_get_free_heap_size() { return 128000; }
void *heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
}
