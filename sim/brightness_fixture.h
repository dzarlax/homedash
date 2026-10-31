#pragma once
#include <stdint.h>
namespace brightness_fixture {
extern bool ready, time_valid, preview;
extern int schedule_calls, clear_calls;
extern uint8_t day, night;
extern bool is_night, wake;
}
