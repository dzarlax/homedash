#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "touch.h"
#include "lvgl.h"
#include "rgb_lcd_port.h"
#include "gt911.h"

#define LVGL_PORT_H_RES             (LCD_H_RES)
#define LVGL_PORT_V_RES             (LCD_V_RES)
#define LVGL_PORT_TICK_PERIOD_MS    (2)

#define LVGL_PORT_TASK_MAX_DELAY_MS (500)
#define LVGL_PORT_TASK_MIN_DELAY_MS (10)
#define LVGL_PORT_TASK_STACK_SIZE   (10 * 1024)
#define LVGL_PORT_TASK_PRIORITY     (2)
#define LVGL_PORT_TASK_CORE         (1)

#define LVGL_PORT_BUFFER_MALLOC_CAPS    (MALLOC_CAP_SPIRAM)
#define LVGL_PORT_BUFFER_HEIGHT         (50)

// Anti-tearing: mode 3 = double-buffer + direct-mode (recommended)
#define LVGL_PORT_AVOID_TEAR_ENABLE     (1)
#if LVGL_PORT_AVOID_TEAR_ENABLE
#define LVGL_PORT_AVOID_TEAR_MODE       (3)

#define EXAMPLE_LVGL_PORT_ROTATION_DEGREE  (0)

#if LVGL_PORT_AVOID_TEAR_MODE == 1
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 2
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
#define LVGL_PORT_FULL_REFRESH          (1)
#elif LVGL_PORT_AVOID_TEAR_MODE == 3
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (2)
#define LVGL_PORT_DIRECT_MODE           (1)
#endif

#if EXAMPLE_LVGL_PORT_ROTATION_DEGREE != 0
  #if EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 90
  #define EXAMPLE_LVGL_PORT_ROTATION_90   (1)
  #elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 180
  #define EXAMPLE_LVGL_PORT_ROTATION_180  (1)
  #elif EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 270
  #define EXAMPLE_LVGL_PORT_ROTATION_270  (1)
  #endif
  #ifdef LVGL_PORT_LCD_RGB_BUFFER_NUMS
  #undef LVGL_PORT_LCD_RGB_BUFFER_NUMS
  #define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (3)
  #endif
#else
  #define EXAMPLE_LVGL_PORT_ROTATION_0    (1)
#endif

#else
#define LVGL_PORT_LCD_RGB_BUFFER_NUMS   (1)
#define LVGL_PORT_FULL_REFRESH          (0)
#define LVGL_PORT_DIRECT_MODE           (0)
#endif

// Ensure macros are defined for conditional compilation
#ifndef LVGL_PORT_FULL_REFRESH
#define LVGL_PORT_FULL_REFRESH          (0)
#endif
#ifndef LVGL_PORT_DIRECT_MODE
#define LVGL_PORT_DIRECT_MODE           (0)
#endif

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle);
bool lvgl_port_lock(int timeout_ms);
void lvgl_port_unlock(void);
bool lvgl_port_notify_rgb_vsync(void);
void lvgl_port_task_start(void);
