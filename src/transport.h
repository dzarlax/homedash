#pragma once

#include <stdint.h>
#include <stdbool.h>

#define TRANSPORT_MAX_VEHICLES 5
#define TRANSPORT_NUM_STOPS    2

// Stop indices
#define TRANSPORT_STOP_OUT    0   // stop 89 — outbound (в область)
#define TRANSPORT_STOP_IN     1   // stop 90 — inbound  (в центр)

struct transport_vehicle_t {
    char line_number[8];
    int  seconds_left;
    int  stations_between;
};

struct transport_stop_t {
    transport_vehicle_t vehicles[TRANSPORT_MAX_VEHICLES];
    int count;
};

struct transport_data_t {
    transport_stop_t stops[TRANSPORT_NUM_STOPS];
    bool valid;
};

void transport_fetch_and_update(void);
const transport_data_t *transport_get_data(void);
const char *transport_get_last_error(void);
