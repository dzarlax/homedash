// Desktop simulator stubs for ESP-IDF, hardware, and network-owned requests.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <chrono>
#endif

#include "bridge.h"

static bridge_data_t s_data = {};
static bridge_cal_data_t s_calendar = {};
static char s_last_error[32] = "fixture";

static void set_calendar_for_date(int year, int month, int day)
{
    memset(&s_calendar, 0, sizeof(s_calendar));
    s_calendar.valid = true;
    s_calendar.year = year;
    s_calendar.month = month;
    s_calendar.day = day;

    s_calendar.count = 6;
    s_calendar.events[0] = {"Домашний день", 0, 0, 0, 0, true, 5};
    s_calendar.events[1] = {"Утренний созвон", 9, 30, 10, 0, false, 0};
    s_calendar.events[2] = {"Забрать заказ", 11, 15, 11, 45, false, 1};
    s_calendar.events[3] = {"Обед", 13, 0, 14, 0, false, 2};
    s_calendar.events[4] = {"Тренировка", 18, 30, 19, 30, false, 3};
    s_calendar.events[5] = {"Вечерний обзор", 21, 0, 21, 20, false, 4};

    time_t now;
    time(&now);
    struct tm t = {};
    localtime_r(&now, &t);
    int today_y = t.tm_year + 1900;
    int today_m = t.tm_mon + 1;
    int today_d = t.tm_mday;

    struct tm tomorrow = t;
    tomorrow.tm_mday += 1;
    mktime(&tomorrow);
    int tomorrow_y = tomorrow.tm_year + 1900;
    int tomorrow_m = tomorrow.tm_mon + 1;
    int tomorrow_d = tomorrow.tm_mday;

    if (year == tomorrow_y && month == tomorrow_m && day == tomorrow_d) {
        s_calendar.count = 2;
        s_calendar.events[0] = {"Домашний день", 0, 0, 0, 0, true, 5};
        s_calendar.events[1] = {"Не планировать встречи", 0, 0, 0, 0, true, 0};
    } else if (year != today_y || month != today_m || day != today_d) {
        s_calendar.count = 3;
        s_calendar.events[0] = {"Запланированное событие", 10, 0, 11, 0, false, 0};
        s_calendar.events[1] = {"Встреча вне дома", 15, 30, 16, 30, false, 2};
        s_calendar.events[2] = {"Напоминание", 19, 0, 19, 15, false, 5};
    }
}

extern "C" void sim_init_fixtures(void)
{
    memset(&s_data, 0, sizeof(s_data));

    s_data.ts = 1;

    s_data.weather.valid = true;
    s_data.weather.temp = 24.0f;
    s_data.weather.humidity = 52.0f;
    s_data.weather.wind_speed = 8.0f;
    s_data.weather.weather_code = 2;
    s_data.weather.daily_count = 3;
    s_data.weather.daily[0] = {26.0f, 17.0f, 2};
    s_data.weather.daily[1] = {28.0f, 18.0f, 1};
    s_data.weather.daily[2] = {23.0f, 16.0f, 61};

    s_data.transport.valid = true;
    s_data.transport.stops[0].count = 4;
    s_data.transport.stops[0].vehicles[0] = {"7L", 180, 2};
    s_data.transport.stops[0].vehicles[1] = {"5", 420, 5};
    s_data.transport.stops[0].vehicles[2] = {"14", 780, 8};
    s_data.transport.stops[0].vehicles[3] = {"6", 1020, 12};
    s_data.transport.stops[1].count = 3;
    s_data.transport.stops[1].vehicles[0] = {"6", 240, 3};
    s_data.transport.stops[1].vehicles[1] = {"7L", 540, 6};
    s_data.transport.stops[1].vehicles[2] = {"5", 900, 10};

    s_data.health.valid = true;
    s_data.health.readiness = 82;
    s_data.health.steps = 8420;
    s_data.health.steps_prev = 7600;
    s_data.health.sleep = 7.4f;
    s_data.health.sleep_prev = 6.8f;
    s_data.health.cal = 2180;
    s_data.health.cal_prev = 2020;
    s_data.health.hr = 74;
    s_data.health.rhr = 58;
    s_data.health.hrv = 46;
    s_data.health.spo2 = 98;

    s_data.task_count = 4;
    s_data.tasks_valid = true;
    s_data.tasks[0] = {"Проверить OTA релиз HomeDash", 4, "2026-05-30"};
    s_data.tasks[1] = {"Обсудить автосброс календаря", 3, "2026-05-30"};
    s_data.tasks[2] = {"Купить фильтр для очистителя", 2, "2026-05-31"};
    s_data.tasks[3] = {"Записать идеи по первому экрану", 1, "2026-06-01"};

    s_data.news_count = 4;
    s_data.news_valid = true;
    s_data.news[0] = {"ESP-IDF 5.3 release notes reviewed", "tech", 2};
    s_data.news[1] = {"Belgrade transit delays are moderate", "city", 3};
    s_data.news[2] = {"Weather stays warm through tomorrow", "weather", 4};
    s_data.news[3] = {"Home automations ran normally overnight", "home", 6};

    s_data.light_count = 6;
    s_data.lights_valid = true;
    s_data.lights[0] = {"light.svet_u_divana", "Диван", true, 180};
    s_data.lights[1] = {"light.svet_u_okna", "Окно", false, 0};
    s_data.lights[2] = {"light.office_light", "Кабинет", true, 220};
    s_data.lights[3] = {"light.bedroom_light", "Спальня", false, 0};
    s_data.lights[4] = {"light.yeelink_bslamp2_2272_light", "Лампа", true, 120};
    s_data.lights[5] = {"light.kukhnia", "Кухня", true, 200};

    s_data.sensor_count = 9;
    s_data.sensors_valid = true;
    s_data.sensors[0] = {"CO2 гостиная", "612", "ppm"};
    s_data.sensors[1] = {"CO2 кабинет", "824", "ppm"};
    s_data.sensors[2] = {"PM2.5", "4", "ug/m3"};
    s_data.sensors[3] = {"Влажность", "44", "%"};
    s_data.sensors[4] = {"Влажность", "47", "%"};
    s_data.sensors[5] = {"Температура", "23.2", "C"};
    s_data.sensors[6] = {"Температура", "24.1", "C"};
    s_data.sensors[7] = {"Температура", "22.8", "C"};
    s_data.sensors[8] = {"Влажность", "45", "%"};

    time_t now;
    time(&now);
    struct tm t = {};
    localtime_r(&now, &t);
    set_calendar_for_date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
}

void request_calendar_date(int year, int month, int day)
{
    printf("[SIM] Calendar date requested: %04d-%02d-%02d\n", year, month, day);
    set_calendar_for_date(year, month, day);
}

void request_light_toggle(const char *entity_id)
{
    printf("[SIM] Toggle light requested: %s\n", entity_id ? entity_id : "");
    if (!entity_id) return;

    for (int i = 0; i < s_data.light_count; i++) {
        if (strcmp(s_data.lights[i].entity_id, entity_id) == 0) {
            s_data.lights[i].on = !s_data.lights[i].on;
            return;
        }
    }
}

void request_ota_check(void)
{
    printf("[SIM] OTA check requested (stub only; no firmware update)\n");
}

void bridge_init(const char *, const char *) {}
void bridge_fetch_and_update(void) {}
void bridge_fetch_calendar(int year, int month, int day) { set_calendar_for_date(year, month, day); }
void bridge_toggle_light(const char *entity_id) { request_light_toggle(entity_id); }

const bridge_data_t *bridge_get_data(void) { return &s_data; }
const bridge_cal_data_t *bridge_get_calendar_data(void) { return &s_calendar; }
const char *bridge_get_last_error(void) { return s_last_error; }
const char *bridge_get_url(void) { return "fixture://homedash"; }
const char *bridge_get_api_key(void) { return ""; }

bool bridge_copy_data(bridge_data_t *out)
{
    if (!out) return false;
    *out = s_data;
    return true;
}

bool bridge_copy_calendar_data(bridge_cal_data_t *out)
{
    if (!out) return false;
    *out = s_calendar;
    return true;
}

bool bridge_copy_last_error(char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    snprintf(out, out_size, "%s", s_last_error);
    return true;
}

bool wifi_is_connected(void) { return true; }

extern "C" {
uint32_t esp_get_free_heap_size(void) { return 128u * 1024u; }
uint32_t esp_get_free_internal_heap_size(void) { return 78u * 1024u; }
void *heap_caps_malloc(size_t size, uint32_t) { return malloc(size); }
int64_t esp_timer_get_time(void)
{
#if defined(_WIN32)
    static ULONGLONG start_ms = GetTickCount64();
    return (int64_t)(GetTickCount64() - start_ms) * 1000LL;
#else
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
#endif
}
}
