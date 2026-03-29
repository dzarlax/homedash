#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BRIDGE_MAX_TASKS   8
#define BRIDGE_MAX_NEWS    5
#define BRIDGE_MAX_SENSORS 10
#define BRIDGE_MAX_LIGHTS  8

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
    char title[80];
    int  priority;
    char due[12];   // "2026-03-29"
};

struct bridge_news_t {
    char title[120];
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
};

void bridge_fetch_and_update(void);
void bridge_toggle_light(const char *entity_id);
const bridge_data_t *bridge_get_data(void);
const char *bridge_get_last_error(void);
