#pragma once
#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>
void photo_frame_ui_init(void);
void photo_frame_network_init(const char *url, const char *key);
void photo_frame_update_network(const char *url, const char *key);
void photo_frame_apply_web_preferences(bool enabled, int idle_seconds,
                                       int interval_seconds);
// Call from the LVGL thread to bypass the idle delay for this session.
bool photo_frame_start_now(void);
bool photo_frame_touch(bool pressed);
void photo_frame_filter_touch(bool pressed, uint16_t x, uint16_t y,
                              lv_indev_data_t *data);
void photo_frame_reader_closed(void);
bool photo_frame_pause_for_ota(void);
void photo_frame_resume_after_ota(void);

#ifdef SIMULATOR
void photo_frame_sim_accelerate(void);
bool photo_frame_sim_visible(void);
#endif
