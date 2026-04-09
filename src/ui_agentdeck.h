#pragma once

#include "lvgl.h"
#include "agentdeck.h"

void ui_agentdeck_create(lv_obj_t *parent);
void ui_agentdeck_update(const agentdeck_data_t *data);
