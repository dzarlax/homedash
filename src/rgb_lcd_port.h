#pragma once

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "io_extension.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

// LCD Resolution
#define LCD_H_RES               (1024)
#define LCD_V_RES               (600)
#define LCD_PIXEL_CLOCK_HZ      (30 * 1000 * 1000)

// Color / pixel config
#define LCD_BIT_PER_PIXEL       (16)
#define RGB_BIT_PER_PIXEL       (16)
#define RGB_DATA_WIDTH          (16)
#define LCD_RGB_BUFFER_NUMS     (2)
#define RGB_BOUNCE_BUFFER_SIZE  (LCD_H_RES * 10)

// Sync / control GPIOs
#define LCD_IO_RGB_DISP         (-1)
#define LCD_IO_RGB_VSYNC        (GPIO_NUM_3)
#define LCD_IO_RGB_HSYNC        (GPIO_NUM_46)
#define LCD_IO_RGB_DE           (GPIO_NUM_5)
#define LCD_IO_RGB_PCLK         (GPIO_NUM_7)

// Blue data [B3..B7]
#define LCD_IO_RGB_DATA0        (GPIO_NUM_14)  // B3
#define LCD_IO_RGB_DATA1        (GPIO_NUM_38)  // B4
#define LCD_IO_RGB_DATA2        (GPIO_NUM_18)  // B5
#define LCD_IO_RGB_DATA3        (GPIO_NUM_17)  // B6
#define LCD_IO_RGB_DATA4        (GPIO_NUM_10)  // B7

// Green data [G2..G7]
#define LCD_IO_RGB_DATA5        (GPIO_NUM_39)  // G2
#define LCD_IO_RGB_DATA6        (GPIO_NUM_0)   // G3
#define LCD_IO_RGB_DATA7        (GPIO_NUM_45)  // G4
#define LCD_IO_RGB_DATA8        (GPIO_NUM_48)  // G5
#define LCD_IO_RGB_DATA9        (GPIO_NUM_47)  // G6
#define LCD_IO_RGB_DATA10       (GPIO_NUM_21)  // G7

// Red data [R3..R7]
#define LCD_IO_RGB_DATA11       (GPIO_NUM_1)   // R3
#define LCD_IO_RGB_DATA12       (GPIO_NUM_2)   // R4
#define LCD_IO_RGB_DATA13       (GPIO_NUM_42)  // R5
#define LCD_IO_RGB_DATA14       (GPIO_NUM_41)  // R6
#define LCD_IO_RGB_DATA15       (GPIO_NUM_40)  // R7

esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_init();
void wavesahre_rgb_lcd_bl_on();
void wavesahre_rgb_lcd_bl_off();
void wavesahre_rgb_lcd_display(uint8_t *Image);
void waveshare_get_frame_buffer(void **buf1, void **buf2);
