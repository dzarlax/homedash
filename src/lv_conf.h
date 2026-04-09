/**
 * @file lv_conf.h
 * Configuration file for LVGL v9.x — HomeDash ESP32-S3 1024x600
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH 16

/*=========================
   MEMORY SETTINGS
 *=========================*/
/* Use standard C malloc — on ESP32-S3 with PSRAM, malloc uses internal
   SRAM for small allocs and falls back to PSRAM for larger ones.
   This frees internal SRAM for mbedTLS/DMA. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN

/*====================
   OS / THREADING
 *====================*/
/* No OS integration — lvgl_port manages its own mutex */
#define LV_USE_OS LV_OS_NONE

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DEF_REFR_PERIOD  33   /* ~30 fps */
#define LV_DPI_DEF          130

/*=======================
   DRAWING
 *=======================*/
#define LV_DRAW_BUF_STRIDE_ALIGN    1
#define LV_DRAW_BUF_ALIGN           4
#define LV_USE_DRAW_SW              1
#define LV_USE_DRAW_SW_ASM          LV_DRAW_SW_ASM_NONE

/*=======================
   CACHE
 *=======================*/
#define LV_CACHE_DEF_SIZE           0

/*=======================
   INDEV
 *=======================*/
#define LV_INDEV_DEF_READ_PERIOD        30
#define LV_INDEV_DEF_SCROLL_LIMIT       10
#define LV_INDEV_DEF_SCROLL_THROW       10
#define LV_INDEV_DEF_LONG_PRESS_TIME    400
#define LV_INDEV_DEF_LONG_PRESS_REP_TIME 100
#define LV_INDEV_DEF_GESTURE_LIMIT      50
#define LV_INDEV_DEF_GESTURE_MIN_VELOCITY 3

/*-------------
 * Logging
 *-----------*/
#define LV_USE_LOG 0
#if LV_USE_LOG
    #define LV_LOG_LEVEL    LV_LOG_LEVEL_WARN
    #define LV_LOG_PRINTF   0
#endif

/*-------------
 * Asserts
 *-----------*/
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1
#define LV_USE_ASSERT_STYLE         0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ           0
#define LV_ASSERT_HANDLER_INCLUDE   <stdint.h>
#define LV_ASSERT_HANDLER           while(1);

/*==================
 *   FONT USAGE
 *===================*/
#define LV_FONT_MONTSERRAT_8   0
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  0
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_26  0
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_MONTSERRAT_30  0
#define LV_FONT_MONTSERRAT_32  1
#define LV_FONT_MONTSERRAT_34  0
#define LV_FONT_MONTSERRAT_36  0
#define LV_FONT_MONTSERRAT_38  0
#define LV_FONT_MONTSERRAT_40  0
#define LV_FONT_MONTSERRAT_42  0
#define LV_FONT_MONTSERRAT_44  0
#define LV_FONT_MONTSERRAT_46  0
#define LV_FONT_MONTSERRAT_48  1

#define LV_FONT_UNSCII_8  0
#define LV_FONT_UNSCII_16 0

#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(font_montserrat_16_cyr); LV_FONT_DECLARE(font_montserrat_24_cyr);
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_FONT_PLACEHOLDER 1

/*=================
 *  TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC                      LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS              " ,.;:-_"
#define LV_TXT_LINE_BREAK_LONG_LEN      0
#define LV_TXT_COLOR_CMD                "#"
#define LV_USE_BIDI                     0
#define LV_USE_ARABIC_PERSIAN_CHARS     0

/*==================
 *  WIDGET USAGE
 *================*/
#define LV_USE_ARC          1
#define LV_USE_BAR          1
#define LV_USE_BTN          1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CANVAS       1
#define LV_USE_CHECKBOX     1
#define LV_USE_DROPDOWN     1
#define LV_USE_IMAGE        1
#define LV_USE_LABEL        1
#if LV_USE_LABEL
    #define LV_LABEL_TEXT_SELECTION 1
    #define LV_LABEL_LONG_TXT_HINT  1
#endif
#define LV_USE_LINE         1
#define LV_USE_ROLLER       1
#if LV_USE_ROLLER
    #define LV_ROLLER_INF_PAGES 7
#endif
#define LV_USE_SLIDER       1
#define LV_USE_SWITCH       1
#define LV_USE_TEXTAREA     1
#if LV_USE_TEXTAREA
    #define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500
#endif
#define LV_USE_TABLE        1

/*==================
 * EXTRA COMPONENTS
 *==================*/
#define LV_USE_ANIMIMG      0
#define LV_USE_CALENDAR     1
#if LV_USE_CALENDAR
    #define LV_CALENDAR_WEEK_STARTS_MONDAY 1
    #if LV_CALENDAR_WEEK_STARTS_MONDAY
        #define LV_CALENDAR_DEFAULT_DAY_NAMES {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"}
    #else
        #define LV_CALENDAR_DEFAULT_DAY_NAMES {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"}
    #endif
    #define LV_CALENDAR_DEFAULT_MONTH_NAMES {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"}
    #define LV_USE_CALENDAR_HEADER_ARROW    1
    #define LV_USE_CALENDAR_HEADER_DROPDOWN 1
#endif
#define LV_USE_CHART        1
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     1
#define LV_USE_LED          1
#define LV_USE_LIST         1
#define LV_USE_MENU         1
#define LV_USE_MSGBOX       1
#define LV_USE_SPAN         1
#if LV_USE_SPAN
    #define LV_SPAN_SNIPPET_STACK_SIZE 64
#endif
#define LV_USE_SPINBOX      1
#define LV_USE_SPINNER      1
#define LV_USE_TABVIEW      1
#define LV_USE_TILEVIEW     1
#define LV_USE_WIN          1

/*-----------
 * Themes
 *----------*/
#define LV_USE_THEME_DEFAULT 1
#if LV_USE_THEME_DEFAULT
    #define LV_THEME_DEFAULT_DARK               0
    #define LV_THEME_DEFAULT_GROW               1
    #define LV_THEME_DEFAULT_TRANSITION_TIME    80
#endif
#define LV_USE_THEME_SIMPLE 1
#define LV_USE_THEME_MONO   1

/*-----------
 * Layouts
 *----------*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*---------------------
 * 3rd party libraries
 *--------------------*/
#define LV_USE_FS_STDIO     0
#define LV_USE_FS_POSIX     0
#define LV_USE_PNG          0
#define LV_USE_BMP          0
#define LV_USE_SJPG         0
#define LV_USE_GIF          0
#define LV_USE_QRCODE       0
#define LV_USE_FREETYPE     0
#define LV_USE_TINY_TTF     0
#define LV_USE_RLOTTIE      0
#define LV_USE_FFMPEG       0

/*-----------
 * Others
 *----------*/
#define LV_USE_SNAPSHOT     0
#define LV_USE_MONKEY       0
#define LV_USE_GRIDNAV      0
#define LV_USE_IME_PINYIN   0

/*==================
 * EXAMPLES / DEMOS
 *==================*/
#define LV_BUILD_EXAMPLES   0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_STRESS  0
#define LV_USE_DEMO_MUSIC   0

#endif /* LV_CONF_H */
