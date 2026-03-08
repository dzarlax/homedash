#pragma once

#include <stdint.h>
#include <stdbool.h>

#define HA_CAL_MAX_EVENTS 20

struct ha_cal_event_t {
    char summary[64];
    uint8_t start_hour;
    uint8_t start_min;
    uint8_t end_hour;
    uint8_t end_min;
    bool    all_day;
    int     sort_key;   // 0 for all-day, otherwise HHMM
    uint8_t cal_idx;    // calendar index for color-coding
};

struct ha_cal_data_t {
    ha_cal_event_t events[HA_CAL_MAX_EVENTS];
    int   count;
    bool  valid;
    int   year;     // which date these events are for
    int   month;
    int   day;
};

void ha_calendar_fetch_and_update(void);
void ha_calendar_fetch_for_date(int year, int month, int day);
const ha_cal_data_t *ha_calendar_get_data(void);
const char *ha_calendar_get_last_error(void);
