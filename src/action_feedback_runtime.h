#pragma once
#include "action_feedback.h"
// Called under LVGL lock; UI must not retain returned pointers across calls.
uint32_t request_home_action(const char *entity, const char *action, float target, const char *mode);
const action_record_t *home_action_find(const char *entity);
void home_action_tick(void);
