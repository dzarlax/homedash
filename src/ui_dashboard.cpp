#include "ui_dashboard.h"
#include "config.h"
#include "wifi_manager.h"
#include "bridge.h"
#include "weather_icons.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

// Declared in main.cpp
extern void request_calendar_date(int year, int month, int day);
extern void request_light_toggle(const char *entity_id);

// Custom font with Cyrillic support (for HA calendar events)
LV_FONT_DECLARE(font_montserrat_16_cyr);
LV_FONT_DECLARE(font_montserrat_24_cyr);

// --- UI element references ---
static lv_obj_t *lbl_datetime    = NULL;
static lv_obj_t *lbl_topbar_temp = NULL;
static lv_obj_t *lbl_bottom      = NULL;
static lv_obj_t *calendar        = NULL;

// Weather card elements
static lv_obj_t *weather_canvas     = NULL;
static lv_obj_t *lbl_weather_main   = NULL;   // "Clear  5C"
static lv_obj_t *lbl_weather_detail = NULL;   // "H:78%  W:12km/h  Tmrw: 8/2C"
static lv_color_t *canvas_buf       = NULL;

// Schedule elements
static lv_obj_t *lbl_sched_title = NULL;
#define MAX_EVENT_LINES 16
static lv_obj_t *lbl_events[MAX_EVENT_LINES] = {};
static lv_obj_t *events_scroll = NULL;  // scrollable container for events
static lv_obj_t *lbl_no_events = NULL;
static lv_obj_t *btn_today     = NULL;
static lv_obj_t *now_line      = NULL;   // "current time" indicator line
static lv_obj_t *right_panel_ref = NULL; // for positioning now_line

// Transport panel elements
static lv_obj_t *lbl_transport_out = NULL;   // stop 89 — outbound
static lv_obj_t *lbl_transport_in  = NULL;   // stop 90 — inbound

// Tileview
static lv_obj_t *tileview = NULL;

// Page 2: Health + Tasks + News (redesigned)
// Readiness arc
static lv_obj_t *arc_readiness = NULL;
static lv_obj_t *lbl_readiness_val = NULL;
static lv_obj_t *lbl_readiness_label = NULL;

// Metric cards: value + label + trend
#define NUM_METRIC_CARDS 7
struct metric_card_t {
    lv_obj_t *container;
    lv_obj_t *lbl_value;
    lv_obj_t *lbl_name;
    lv_obj_t *lbl_trend;
};
static metric_card_t metric_cards[NUM_METRIC_CARDS] = {};

// Tasks
#define MAX_TASK_LINES 8
static lv_obj_t *task_prio_bars[MAX_TASK_LINES] = {};
static lv_obj_t *lbl_task_lines[MAX_TASK_LINES] = {};
static lv_obj_t *lbl_no_tasks = NULL;

// News
#define MAX_NEWS_LINES 5
static lv_obj_t *news_dots[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_news_lines[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_news_age[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_no_news = NULL;

// Design colors (deeper palette)
#define COLOR_CARD      lv_color_hex(0x1E2140)
#define COLOR_GOOD      lv_color_hex(0x4CAF50)
#define COLOR_WARN      lv_color_hex(0xFFC107)
#define COLOR_BAD       lv_color_hex(0xEF5350)
// Page 3: HA Control — room-based layout
#define MAX_ROOMS 4
#define MAX_ROOM_LIGHTS 3
#define MAX_ROOM_SENSORS 6

struct room_def_t {
    const char *name;
    const char *light_ids[MAX_ROOM_LIGHTS];
    int light_count;
    const char *sensor_ids[MAX_ROOM_SENSORS];
    int sensor_count;
};

static const room_def_t ROOMS[MAX_ROOMS] = {
    {"Гостиная",
     {"light.svet_u_divana", "light.svet_u_okna", NULL}, 2,
     {"sensor.gostinaia_airq_co2", "sensor.zhimi_ca4_90f5_relative_humidity_2", NULL, NULL, NULL, NULL}, 2},
    {"Кабинет",
     {"light.office_light", NULL, NULL}, 1,
     {"sensor.co2_sensor_co2", "sensor.zhimi_vb4_f663_pm25_density", "sensor.zhimi_vb4_f663_relative_humidity", "sensor.zhimi_vb4_f663_temperature", "sensor.aqara_sensor_temperature", "sensor.aqara_sensor_humidity"}, 6},
    {"Спальня",
     {"light.bedroom_light", "light.yeelink_bslamp2_2272_light", NULL}, 2,
     {"sensor.purifier_humidifier_humidity", "sensor.purifier_humidifier_temperature", NULL, NULL, NULL, NULL}, 2},
    {"Кухня",
     {"light.kukhnia", NULL, NULL}, 1,
     {NULL, NULL, NULL, NULL, NULL, NULL}, 0},
};

static lv_obj_t *room_light_btns[MAX_ROOMS][MAX_ROOM_LIGHTS] = {};
static lv_obj_t *room_light_labels[MAX_ROOMS][MAX_ROOM_LIGHTS] = {};
// Sensor cards per room (value + name labels)
static lv_obj_t *room_sensor_cards[MAX_ROOMS][MAX_ROOM_SENSORS] = {};
static lv_obj_t *room_sensor_val_lbl[MAX_ROOMS][MAX_ROOM_SENSORS] = {};
static lv_obj_t *room_sensor_name_lbl[MAX_ROOMS][MAX_ROOM_SENSORS] = {};

// Page indicator dots
static lv_obj_t *dot_indicators[3] = {};

#define COLOR_NOW lv_color_hex(0xFF4444)  // red "now" line

// Track last calendar date to avoid unnecessary updates
static int last_cal_year = 0;
static int last_cal_mon  = 0;
static int last_cal_day  = 0;

// Currently selected date for schedule display
static int sel_year  = 0;
static int sel_month = 0;
static int sel_day   = 0;

// Track last weather code to avoid unnecessary redraws
static int last_weather_code = -1;

static const char *DOW_NAMES[] = {"Bc", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
static const char *MONTH_NAMES[] = {"Янв", "Фев", "Мар", "Апр", "Май", "Июн",
                                     "Июл", "Авг", "Сен", "Окт", "Ноя", "Дек"};

#define COLOR_BG        lv_color_hex(0x1A1A2E)
#define COLOR_PANEL     lv_color_hex(0x16213E)
#define COLOR_ACCENT    lv_color_hex(0x0F3460)
#define COLOR_TEXT      lv_color_hex(0xE0E0E0)
#define COLOR_TEXT_DIM  lv_color_hex(0x8888AA)
#define COLOR_HIGHLIGHT lv_color_hex(0x53A8E2)

// Calendar event colors (per calendar index)
#define NUM_CAL_COLORS 6
static const lv_color_t CAL_COLORS[NUM_CAL_COLORS] = {
    {.full = 0x53A8},  // placeholder — initialized at runtime
};
static lv_color_t s_cal_colors[NUM_CAL_COLORS];
static bool s_cal_colors_init = false;

static lv_color_t cal_color(uint8_t idx) {
    if (!s_cal_colors_init) {
        s_cal_colors[0] = lv_color_hex(0x5BC0EB);  // sky blue
        s_cal_colors[1] = lv_color_hex(0x66BB6A);  // green
        s_cal_colors[2] = lv_color_hex(0xFFA726);  // orange
        s_cal_colors[3] = lv_color_hex(0xAB47BC);  // purple
        s_cal_colors[4] = lv_color_hex(0xEF5350);  // red
        s_cal_colors[5] = lv_color_hex(0x26C6DA);  // cyan
        s_cal_colors_init = true;
    }
    return s_cal_colors[idx % NUM_CAL_COLORS];
}

#define ICON_SIZE 48

static void timer_time_cb(lv_timer_t *timer)
{
    (void)timer;
    ui_dashboard_update_time();
}

static void timer_weather_cb(lv_timer_t *timer)
{
    (void)timer;
    const bridge_data_t *d = bridge_get_data();
    ui_dashboard_update_weather(&d->weather);
}

static void timer_ha_cal_cb(lv_timer_t *timer)
{
    (void)timer;
    const bridge_cal_data_t *d = bridge_get_calendar_data();
    ui_dashboard_update_ha_calendar(d);
}

static void timer_transport_cb(lv_timer_t *timer)
{
    (void)timer;
    const bridge_data_t *d = bridge_get_data();
    ui_dashboard_update_transport(&d->transport);
}

static void timer_bridge_cb(lv_timer_t *timer)
{
    (void)timer;
    const bridge_data_t *d = bridge_get_data();
    ui_dashboard_update_bridge(d);
    ui_dashboard_update_ha(d);
}

static void update_dot_indicators(int active)
{
    for (int i = 0; i < 3; i++) {
        if (dot_indicators[i]) {
            lv_obj_set_style_bg_opa(dot_indicators[i],
                (i == active) ? LV_OPA_COVER : LV_OPA_50, 0);
        }
    }
}

static void tileview_changed_cb(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    lv_obj_t *tile = lv_tileview_get_tile_act(tv);
    int col = lv_obj_get_x(tile) / 1024;
    update_dot_indicators(col);
}

static bool is_today(int y, int m, int d)
{
    time_t now;
    time(&now);
    struct tm tinfo;
    localtime_r(&now, &tinfo);
    return (y == tinfo.tm_year + 1900 && m == tinfo.tm_mon + 1 && d == tinfo.tm_mday);
}

static void update_schedule_title(int y, int m, int d)
{
    if (!lbl_sched_title) return;
    if (is_today(y, m, d)) {
        lv_label_set_text(lbl_sched_title, "Расписание");
        if (btn_today) lv_obj_add_flag(btn_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        char buf[48];
        const char *mon = (m >= 1 && m <= 12) ? MONTH_NAMES[m - 1] : "???";
        snprintf(buf, sizeof(buf), "Расписание: %s %d", mon, d);
        lv_label_set_text(lbl_sched_title, buf);
        if (btn_today) lv_obj_clear_flag(btn_today, LV_OBJ_FLAG_HIDDEN);
    }
}

static void calendar_click_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED) return;

    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(calendar, &date) != LV_RES_OK) return;

    sel_year  = date.year;
    sel_month = date.month;
    sel_day   = date.day;

    update_schedule_title(sel_year, sel_month, sel_day);
    request_calendar_date(sel_year, sel_month, sel_day);
}

static void btn_today_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    time_t now;
    time(&now);
    struct tm tinfo;
    localtime_r(&now, &tinfo);

    sel_year  = tinfo.tm_year + 1900;
    sel_month = tinfo.tm_mon + 1;
    sel_day   = tinfo.tm_mday;

    update_schedule_title(sel_year, sel_month, sel_day);
    request_calendar_date(sel_year, sel_month, sel_day);
}

static void create_page2(lv_obj_t *tile);
static void create_page3(lv_obj_t *tile);

void ui_dashboard_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Create tileview for horizontal swipe between pages
    tileview = lv_tileview_create(scr);
    lv_obj_set_size(tileview, 1024, 600);
    lv_obj_set_pos(tileview, 0, 0);
    lv_obj_set_style_bg_opa(tileview, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(tileview, tileview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *tile1 = lv_tileview_add_tile(tileview, 0, 0, LV_DIR_RIGHT);
    lv_obj_t *tile2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
    lv_obj_t *tile3 = lv_tileview_add_tile(tileview, 2, 0, LV_DIR_LEFT);

    // Page indicator dots (bottom center, above bottom bar)
    for (int i = 0; i < 3; i++) {
        dot_indicators[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(dot_indicators[i]);
        lv_obj_set_size(dot_indicators[i], 8, 8);
        lv_obj_set_pos(dot_indicators[i], 496 + i * 16, 572);
        lv_obj_set_style_bg_color(dot_indicators[i], COLOR_TEXT, 0);
        lv_obj_set_style_bg_opa(dot_indicators[i], (i == 0) ? LV_OPA_COVER : LV_OPA_50, 0);
        lv_obj_set_style_radius(dot_indicators[i], LV_RADIUS_CIRCLE, 0);
    }

    // ===== PAGE 1: Main Dashboard (existing) =====
    lv_obj_t *page = tile1;

    // ---- TOP BAR (60px) ----
    lv_obj_t *top_bar = lv_obj_create(page);
    lv_obj_remove_style_all(top_bar);
    lv_obj_set_size(top_bar, 1024, 60);
    lv_obj_set_pos(top_bar, 0, 0);
    lv_obj_set_style_bg_color(top_bar, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_datetime = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_datetime, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_datetime, &font_montserrat_24_cyr, 0);
    lv_obj_set_width(lbl_datetime, 500);
    lv_label_set_long_mode(lbl_datetime, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_datetime, "...");
    lv_obj_set_pos(lbl_datetime, 15, 15);

    lbl_topbar_temp = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_topbar_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_topbar_temp, &font_montserrat_24_cyr, 0);
    lv_obj_set_width(lbl_topbar_temp, 500);
    lv_label_set_long_mode(lbl_topbar_temp, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_topbar_temp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_topbar_temp, WEATHER_CITY);
    lv_obj_set_pos(lbl_topbar_temp, 509, 5);

    // Weather detail in top bar (second line)
    lbl_weather_detail = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_weather_detail, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_weather_detail, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_weather_detail, 500);
    lv_label_set_long_mode(lbl_weather_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_weather_detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_weather_detail, "");
    lv_obj_set_pos(lbl_weather_detail, 509, 35);

    // ---- LEFT PANEL (weather + calendar) ----
    lv_obj_t *cal_panel = lv_obj_create(page);
    lv_obj_remove_style_all(cal_panel);
    lv_obj_set_size(cal_panel, 410, 510);
    lv_obj_set_pos(cal_panel, 5, 65);
    lv_obj_set_style_bg_color(cal_panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(cal_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cal_panel, 8, 0);
    lv_obj_clear_flag(cal_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Weather detail (moved to top bar — lbl_weather_detail created there)
    // Canvas kept but hidden in panel (used by weather_icon_draw on ESP32)
    weather_canvas = lv_canvas_create(cal_panel);
    canvas_buf = (lv_color_t *)heap_caps_malloc(ICON_SIZE * ICON_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (canvas_buf) {
        lv_canvas_set_buffer(weather_canvas, canvas_buf, ICON_SIZE, ICON_SIZE, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(weather_canvas, COLOR_PANEL, LV_OPA_COVER);
    }
    lv_obj_add_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);

    // Calendar widget (full panel)
    calendar = lv_calendar_create(cal_panel);
    lv_obj_set_size(calendar, 390, 490);
    lv_obj_align(calendar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(calendar, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(calendar, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(calendar, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(calendar, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_border_width(calendar, 0, 0);

    lv_obj_t *cal_header = lv_calendar_header_arrow_create(calendar);
    lv_obj_set_style_text_color(cal_header, COLOR_TEXT, 0);

    lv_calendar_set_today_date(calendar, 2026, 2, 22);
    lv_calendar_set_showed_date(calendar, 2026, 2);

    lv_obj_add_event_cb(calendar, calendar_click_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // ---- RIGHT PANEL (schedule + transport) ----
    right_panel_ref = lv_obj_create(page);
    lv_obj_t *right_panel = right_panel_ref;
    lv_obj_remove_style_all(right_panel);
    lv_obj_set_size(right_panel, 599, 510);
    lv_obj_set_pos(right_panel, 420, 65);
    lv_obj_set_style_bg_color(right_panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(right_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(right_panel, 8, 0);
    lv_obj_clear_flag(right_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Schedule title
    lbl_sched_title = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_sched_title, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_sched_title, &font_montserrat_24_cyr, 0);
    lv_label_set_text(lbl_sched_title, "Расписание");
    lv_obj_set_pos(lbl_sched_title, 20, 10);

    // "Сегодня" button
    btn_today = lv_btn_create(right_panel);
    lv_obj_set_size(btn_today, 70, 28);
    lv_obj_set_pos(btn_today, 505, 8);
    lv_obj_set_style_bg_color(btn_today, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_today, 4, 0);
    lv_obj_set_style_pad_all(btn_today, 0, 0);
    lv_obj_add_event_cb(btn_today, btn_today_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(btn_today, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn_lbl = lv_label_create(btn_today);
    lv_obj_set_style_text_color(btn_lbl, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(btn_lbl, &font_montserrat_16_cyr, 0);
    lv_label_set_text(btn_lbl, "Сегодня");
    lv_obj_center(btn_lbl);

    // Scrollable event area (y=42, height=288 — fits 8 visible, scrolls for more)
    events_scroll = lv_obj_create(right_panel);
    lv_obj_remove_style_all(events_scroll);
    lv_obj_set_size(events_scroll, 579, 288);
    lv_obj_set_pos(events_scroll, 0, 42);
    lv_obj_set_style_bg_opa(events_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(events_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(events_scroll, 3, 0);
    lv_obj_set_style_pad_left(events_scroll, 20, 0);
    lv_obj_set_scroll_dir(events_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(events_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(events_scroll, COLOR_TEXT_DIM, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(events_scroll, 4, LV_PART_SCROLLBAR);

    for (int i = 0; i < MAX_EVENT_LINES; i++) {
        lbl_events[i] = lv_label_create(events_scroll);
        lv_obj_set_style_text_color(lbl_events[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_events[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_width(lbl_events[i], 539);
        lv_label_set_long_mode(lbl_events[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(lbl_events[i], "");
        lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
    }

    // "No events" fallback
    lbl_no_events = lv_label_create(events_scroll);
    lv_obj_set_style_text_color(lbl_no_events, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_events, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_no_events, "Нет событий");

    // "Now" time indicator line
    now_line = lv_obj_create(events_scroll);
    lv_obj_remove_style_all(now_line);
    lv_obj_set_size(now_line, 539, 2);
    lv_obj_set_style_bg_color(now_line, COLOR_NOW, 0);
    lv_obj_set_style_bg_opa(now_line, LV_OPA_COVER, 0);
    lv_obj_clear_flag(now_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);

    // ---- TRANSPORT SECTION (below events in right panel) ----
    // Separator
    lv_obj_t *tr_sep = lv_obj_create(right_panel);
    lv_obj_remove_style_all(tr_sep);
    lv_obj_set_size(tr_sep, 559, 2);
    lv_obj_set_pos(tr_sep, 20, 335);
    lv_obj_set_style_bg_color(tr_sep, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(tr_sep, LV_OPA_COVER, 0);
    lv_obj_clear_flag(tr_sep, LV_OBJ_FLAG_SCROLLABLE);

    // Transport title
    lv_obj_t *lbl_tr_title = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_tr_title, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_tr_title, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_tr_title, "Джерам");
    lv_obj_set_pos(lbl_tr_title, 20, 345);

    // Outbound (stop 89)
    lv_obj_t *lbl_out_dir = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_out_dir, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_out_dir, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_out_dir, LV_SYMBOL_RIGHT " Область");
    lv_obj_set_pos(lbl_out_dir, 20, 375);

    lbl_transport_out = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_transport_out, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_transport_out, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_transport_out, 440);
    lv_label_set_long_mode(lbl_transport_out, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(lbl_transport_out, true);
    lv_label_set_text(lbl_transport_out, "---");
    lv_obj_set_pos(lbl_transport_out, 130, 373);

    // Inbound (stop 90)
    lv_obj_t *lbl_in_dir = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_in_dir, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_in_dir, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_in_dir, LV_SYMBOL_LEFT " Центр");
    lv_obj_set_pos(lbl_in_dir, 20, 405);

    lbl_transport_in = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_transport_in, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_transport_in, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_transport_in, 440);
    lv_label_set_long_mode(lbl_transport_in, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(lbl_transport_in, true);
    lv_label_set_text(lbl_transport_in, "---");
    lv_obj_set_pos(lbl_transport_in, 130, 403);

    // ---- BOTTOM BAR (20px) ----
    lv_obj_t *bottom_bar = lv_obj_create(page);
    lv_obj_remove_style_all(bottom_bar);
    lv_obj_set_size(bottom_bar, 1024, 20);
    lv_obj_set_pos(bottom_bar, 0, 580);
    lv_obj_set_style_bg_color(bottom_bar, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(bottom_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bottom_bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_bottom = lv_label_create(bottom_bar);
    lv_obj_set_style_text_color(lbl_bottom, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_bottom, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_bottom, 994);
    lv_label_set_long_mode(lbl_bottom, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_bottom, "WiFi: ---");
    lv_obj_set_pos(lbl_bottom, 10, 2);

    // ===== PAGE 2: Health + Tasks + News =====
    create_page2(tile2);

    // ===== PAGE 3: HA Control =====
    create_page3(tile3);

    // Timers
    lv_timer_create(timer_time_cb, 5000, NULL);
    lv_timer_create(timer_weather_cb, 10000, NULL);
    lv_timer_create(timer_ha_cal_cb, 5000, NULL);
    lv_timer_create(timer_transport_cb, 5000, NULL);
    lv_timer_create(timer_bridge_cb, 5000, NULL);

    // Initial update
    ui_dashboard_update_time();
}

// Map WMO weather code to text (moved from weather.cpp)
static const char *weather_code_to_text(int code)
{
    if (code == 0)                        return "Clear";
    if (code >= 1 && code <= 3)           return "Cloudy";
    if (code == 45 || code == 48)         return "Fog";
    if (code >= 51 && code <= 55)         return "Drizzle";
    if (code >= 56 && code <= 57)         return "Frzng Drz";
    if (code >= 61 && code <= 65)         return "Rain";
    if (code >= 66 && code <= 67)         return "Frzng Rain";
    if (code >= 71 && code <= 75)         return "Snow";
    if (code == 77)                       return "Snow Grn";
    if (code >= 80 && code <= 82)         return "Showers";
    if (code >= 85 && code <= 86)         return "Snow Shw";
    if (code >= 95 && code <= 99)         return "Storm";
    return "???";
}

void ui_dashboard_update_weather(const bridge_weather_t *data)
{
    if (!data || !data->valid) return;

    // Update weather icon (only redraw if code changed)
    if (weather_canvas && canvas_buf && data->weather_code != last_weather_code) {
        weather_icon_draw(weather_canvas, data->weather_code);
        last_weather_code = data->weather_code;
    }

    // Top bar: "Belgrade  Cloudy  12C"
    if (lbl_topbar_temp) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s  %s  %.0fC",
                 WEATHER_CITY,
                 weather_code_to_text(data->weather_code),
                 data->temp);
        lv_label_set_text(lbl_topbar_temp, buf);
    }

    // Top bar detail: "H:78%  W:12km/h  Tmrw: 8/2C"
    if (lbl_weather_detail) {
        char buf[96];
        snprintf(buf, sizeof(buf), "H:%.0f%%  W:%.0fkm/h  Tmrw: %.0f/%.0fC",
                 data->humidity,
                 data->wind_speed,
                 data->daily[1].temp_max,
                 data->daily[1].temp_min);
        lv_label_set_text(lbl_weather_detail, buf);
    }
}

void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data)
{
    if (!data || !data->valid) {
        if (lbl_no_events) lv_obj_clear_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < MAX_EVENT_LINES; i++) {
            if (lbl_events[i]) lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    // Update the schedule title to reflect fetched date
    if (data->year > 0) {
        sel_year  = data->year;
        sel_month = data->month;
        sel_day   = data->day;
        update_schedule_title(sel_year, sel_month, sel_day);

        // Update "no events" text based on date
        if (lbl_no_events) {
            if (is_today(sel_year, sel_month, sel_day)) {
                lv_label_set_text(lbl_no_events, "Нет событий");
            } else {
                char buf[48];
                const char *mon = (sel_month >= 1 && sel_month <= 12) ? MONTH_NAMES[sel_month - 1] : "???";
                snprintf(buf, sizeof(buf), "Нет событий %s %d", mon, sel_day);
                lv_label_set_text(lbl_no_events, buf);
            }
        }
    }

    int display_count = data->count < MAX_EVENT_LINES ? data->count : MAX_EVENT_LINES;

    if (display_count == 0) {
        if (lbl_no_events) lv_obj_clear_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (lbl_no_events) lv_obj_add_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < MAX_EVENT_LINES; i++) {
        if (!lbl_events[i]) continue;

        // Reset background (clear previous "now" highlight)
        lv_obj_set_style_bg_opa(lbl_events[i], LV_OPA_TRANSP, 0);

        if (i < display_count) {
            const bridge_cal_event_t *ev = &data->events[i];
            char buf[96];
            if (ev->all_day) {
                snprintf(buf, sizeof(buf), "Весь день    %s", ev->summary);
            } else {
                snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d  %s",
                         ev->start_hour, ev->start_min,
                         ev->end_hour, ev->end_min,
                         ev->summary);
            }
            lv_label_set_text(lbl_events[i], buf);
            lv_obj_set_style_text_color(lbl_events[i], cal_color(ev->cal_idx), 0);
            lv_obj_clear_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // --- "Now" indicator: highlight current event or show line between events ---
    bool show_now = is_today(sel_year, sel_month, sel_day) && display_count > 0;
    if (show_now) {
        time_t now;
        time(&now);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        int now_min = tinfo.tm_hour * 60 + tinfo.tm_min;

        int highlight_idx = -1;
        int line_before_idx = -1; // show line before this event index
        bool found = false;

        for (int i = 0; i < display_count; i++) {
            const bridge_cal_event_t *ev = &data->events[i];
            if (ev->all_day) continue;

            int ev_start = ev->start_hour * 60 + ev->start_min;
            int ev_end   = ev->end_hour * 60 + ev->end_min;

            if (now_min < ev_start) {
                // Current time is before this event
                line_before_idx = i;
                found = true;
                break;
            } else if (now_min < ev_end) {
                // Current time is during this event
                highlight_idx = i;
                found = true;
                break;
            }
            // else: current time is after this event, continue
        }

        if (!found && display_count > 0) {
            // After all timed events — show line after the last one
            line_before_idx = display_count;
        }

        if (highlight_idx >= 0 && lbl_events[highlight_idx]) {
            // Highlight the current event with a subtle background
            lv_obj_set_style_bg_color(lbl_events[highlight_idx], COLOR_NOW, 0);
            lv_obj_set_style_bg_opa(lbl_events[highlight_idx], LV_OPA_30, 0);
            lv_obj_set_style_radius(lbl_events[highlight_idx], 4, 0);
            if (now_line) lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);
            // Auto-scroll to highlight
            lv_obj_scroll_to_view(lbl_events[highlight_idx], LV_ANIM_ON);
        } else if (line_before_idx >= 0 && now_line) {
            // Move now_line in flex layout to the right position
            lv_obj_move_to_index(now_line, line_before_idx);
            lv_obj_clear_flag(now_line, LV_OBJ_FLAG_HIDDEN);
            // Auto-scroll to now line
            lv_obj_scroll_to_view(now_line, LV_ANIM_ON);
        } else {
            if (now_line) lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        if (now_line) lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);
    }
}

// Recolor: green (close) → yellow → orange → red (far)
static const char *time_color(int mins)
{
    if (mins <= 2)  return "4CAF50";  // green — arriving now
    if (mins <= 5)  return "8BC34A";  // light green
    if (mins <= 8)  return "FFC107";  // yellow
    if (mins <= 12) return "FF9800";  // orange
    if (mins <= 18) return "FF5722";  // deep orange
    return "F44336";                  // red — far away
}

static void format_stop_line(char *buf, int buf_size, const bridge_transport_stop_t *stop)
{
    if (stop->count == 0) {
        snprintf(buf, buf_size, "нет данных");
        return;
    }
    int pos = 0;
    for (int i = 0; i < stop->count && pos < buf_size - 1; i++) {
        const bridge_transport_vehicle_t *v = &stop->vehicles[i];
        int mins = (v->seconds_left + 30) / 60;
        if (mins < 1) mins = 1;
        int written;
        if (i > 0) {
            written = snprintf(buf + pos, buf_size - pos,
                "   #53A8E2 %s# #%s %dmin#",
                v->line_number, time_color(mins), mins);
        } else {
            written = snprintf(buf + pos, buf_size - pos,
                "#53A8E2 %s# #%s %dmin#",
                v->line_number, time_color(mins), mins);
        }
        if (written > 0) pos += written;
    }
}

void ui_dashboard_update_transport(const bridge_transport_t *data)
{
    if (!data || !data->valid) return;

    if (lbl_transport_out) {
        char buf[256];
        format_stop_line(buf, sizeof(buf), &data->stops[0]);
        lv_label_set_text(lbl_transport_out, buf);
    }

    if (lbl_transport_in) {
        char buf[256];
        format_stop_line(buf, sizeof(buf), &data->stops[1]);
        lv_label_set_text(lbl_transport_in, buf);
    }
}

void ui_dashboard_update_time(void)
{
    time_t now;
    time(&now);
    struct tm tinfo;
    localtime_r(&now, &tinfo);

    // Update top bar clock
    if (lbl_datetime) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s, %d %s %d  %02d:%02d:%02d",
                 DOW_NAMES[tinfo.tm_wday],
                 tinfo.tm_mday, MONTH_NAMES[tinfo.tm_mon],
                 tinfo.tm_year + 1900,
                 tinfo.tm_hour, tinfo.tm_min, tinfo.tm_sec);
        lv_label_set_text(lbl_datetime, buf);
    }

    // Update calendar ONLY when date changes (not every second!)
    if (calendar && tinfo.tm_year > 100) {
        int y = tinfo.tm_year + 1900;
        int m = tinfo.tm_mon + 1;
        int d = tinfo.tm_mday;
        if (y != last_cal_year || m != last_cal_mon || d != last_cal_day) {
            lv_calendar_set_today_date(calendar, y, m, d);
            lv_calendar_set_showed_date(calendar, y, m);
            last_cal_year = y;
            last_cal_mon = m;
            last_cal_day = d;
        }
    }

    // Update bottom bar
    if (lbl_bottom) {
        char buf[128];
        snprintf(buf, sizeof(buf), "WiFi: %s  Heap: %.1fKB  Bridge: %s",
                 wifi_is_connected() ? "OK" : "---",
                 esp_get_free_heap_size() / 1024.0f,
                 bridge_get_last_error());
        lv_label_set_text(lbl_bottom, buf);
    }
}

// ===== PAGE 2: Health + Tasks + News =====

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void create_page2(lv_obj_t *tile)
{
    // ===== TOP: Health banner (full width, 275px) =====
    lv_obj_t *health_banner = make_card(tile, 5, 5, 1014, 275);

    // Title
    lv_obj_t *lbl_htitle = lv_label_create(health_banner);
    lv_obj_set_style_text_color(lbl_htitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_htitle, &font_montserrat_24_cyr, 0);
    lv_label_set_text(lbl_htitle, "Здоровье");
    lv_obj_set_pos(lbl_htitle, 15, 8);

    // Readiness arc (left side)
    arc_readiness = lv_arc_create(health_banner);
    lv_obj_set_size(arc_readiness, 140, 140);
    lv_obj_set_pos(arc_readiness, 20, 50);
    lv_arc_set_rotation(arc_readiness, 135);
    lv_arc_set_bg_angles(arc_readiness, 0, 270);
    lv_arc_set_range(arc_readiness, 0, 100);
    lv_arc_set_value(arc_readiness, 0);
    lv_obj_remove_style(arc_readiness, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc_readiness, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_readiness, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_readiness, lv_color_hex(0x2A2A4A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_readiness, COLOR_GOOD, LV_PART_INDICATOR);
    lv_obj_clear_flag(arc_readiness, LV_OBJ_FLAG_CLICKABLE);

    // Readiness value (inside arc)
    lbl_readiness_val = lv_label_create(health_banner);
    lv_obj_set_style_text_color(lbl_readiness_val, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_readiness_val, &lv_font_montserrat_48, 0);
    lv_label_set_text(lbl_readiness_val, "--");
    lv_obj_set_pos(lbl_readiness_val, 58, 85);

    // Readiness label (below arc)
    lbl_readiness_label = lv_label_create(health_banner);
    lv_obj_set_style_text_color(lbl_readiness_label, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_readiness_label, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_readiness_label, "Готовность");
    lv_obj_set_pos(lbl_readiness_label, 45, 200);

    // Row 1: 3 main metric cards (Steps, Sleep, Calories) — bigger
    static const char *row1_names[] = {"Шаги", "Сон", "Калории"};
    int r1_x = 185, r1_w = 190, r1_h = 110, r1_gap = 10;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *mc = make_card(health_banner, r1_x + i * (r1_w + r1_gap), 38, r1_w, r1_h);
        metric_cards[i].lbl_name = lv_label_create(mc);
        lv_obj_set_style_text_color(metric_cards[i].lbl_name, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(metric_cards[i].lbl_name, &font_montserrat_16_cyr, 0);
        lv_obj_set_width(metric_cards[i].lbl_name, r1_w - 16);
        lv_obj_set_style_text_align(metric_cards[i].lbl_name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(metric_cards[i].lbl_name, row1_names[i]);
        lv_obj_set_pos(metric_cards[i].lbl_name, 8, 6);

        metric_cards[i].lbl_value = lv_label_create(mc);
        lv_obj_set_style_text_color(metric_cards[i].lbl_value, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(metric_cards[i].lbl_value, &lv_font_montserrat_32, 0);
        lv_obj_set_width(metric_cards[i].lbl_value, r1_w - 16);
        lv_obj_set_style_text_align(metric_cards[i].lbl_value, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(metric_cards[i].lbl_value, "---");
        lv_obj_set_pos(metric_cards[i].lbl_value, 8, 28);

        metric_cards[i].lbl_trend = lv_label_create(mc);
        lv_obj_set_style_text_color(metric_cards[i].lbl_trend, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(metric_cards[i].lbl_trend, &font_montserrat_16_cyr, 0);
        lv_obj_set_width(metric_cards[i].lbl_trend, r1_w - 16);
        lv_obj_set_style_text_align(metric_cards[i].lbl_trend, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(metric_cards[i].lbl_trend, "");
        lv_obj_set_pos(metric_cards[i].lbl_trend, 8, 72);
        metric_cards[i].container = mc;
    }

    // Remaining space right of row 1: extra wide card
    lv_obj_t *mc_extra = make_card(health_banner, r1_x + 3 * (r1_w + r1_gap), 38, 210, r1_h);
    // Card 3 = HR (live)
    metric_cards[3].lbl_name = lv_label_create(mc_extra);
    lv_obj_set_style_text_color(metric_cards[3].lbl_name, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(metric_cards[3].lbl_name, &font_montserrat_16_cyr, 0);
    lv_label_set_text(metric_cards[3].lbl_name, "Пульс");
    lv_obj_set_pos(metric_cards[3].lbl_name, 8, 6);
    metric_cards[3].lbl_value = lv_label_create(mc_extra);
    lv_obj_set_style_text_color(metric_cards[3].lbl_value, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(metric_cards[3].lbl_value, &lv_font_montserrat_32, 0);
    lv_label_set_text(metric_cards[3].lbl_value, "---");
    lv_obj_set_pos(metric_cards[3].lbl_value, 8, 28);
    metric_cards[3].lbl_trend = lv_label_create(mc_extra);
    lv_obj_set_style_text_font(metric_cards[3].lbl_trend, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(metric_cards[3].lbl_trend, COLOR_TEXT_DIM, 0);
    lv_label_set_text(metric_cards[3].lbl_trend, "");
    lv_obj_set_pos(metric_cards[3].lbl_trend, 8, 72);
    metric_cards[3].container = mc_extra;

    // Row 2: 4 smaller cards (RHR, HRV, SpO2, Resp Rate) — below
    static const char *row2_names[] = {"ЧСС покоя", "ВСР", "SpO2"};
    int r2_x = 185, r2_w = 270, r2_h = 100, r2_gap = 10;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *mc2 = make_card(health_banner, r2_x + i * (r2_w + r2_gap), 158, r2_w, r2_h);
        int idx = 4 + i;
        metric_cards[idx].lbl_name = lv_label_create(mc2);
        lv_obj_set_style_text_color(metric_cards[idx].lbl_name, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(metric_cards[idx].lbl_name, &font_montserrat_16_cyr, 0);
        lv_label_set_text(metric_cards[idx].lbl_name, row2_names[i]);
        lv_obj_set_pos(metric_cards[idx].lbl_name, 8, 6);

        metric_cards[idx].lbl_value = lv_label_create(mc2);
        lv_obj_set_style_text_color(metric_cards[idx].lbl_value, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(metric_cards[idx].lbl_value, &lv_font_montserrat_28, 0);
        lv_label_set_text(metric_cards[idx].lbl_value, "---");
        lv_obj_set_pos(metric_cards[idx].lbl_value, 8, 28);

        metric_cards[idx].lbl_trend = lv_label_create(mc2);
        lv_obj_set_style_text_font(metric_cards[idx].lbl_trend, &font_montserrat_16_cyr, 0);
        lv_obj_set_style_text_color(metric_cards[idx].lbl_trend, COLOR_TEXT_DIM, 0);
        lv_label_set_text(metric_cards[idx].lbl_trend, "");
        lv_obj_set_pos(metric_cards[idx].lbl_trend, 8, 65);
        metric_cards[idx].container = mc2;
    }

    // ===== BOTTOM LEFT: Tasks (500x305) =====
    lv_obj_t *tasks_panel = make_card(tile, 5, 285, 505, 305);

    lv_obj_t *lbl_ttitle = lv_label_create(tasks_panel);
    lv_obj_set_style_text_color(lbl_ttitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_ttitle, &font_montserrat_24_cyr, 0);
    lv_label_set_text(lbl_ttitle, "Задачи");
    lv_obj_set_pos(lbl_ttitle, 15, 10);

    for (int i = 0; i < MAX_TASK_LINES; i++) {
        int y = 38 + i * 32;

        // Priority bar (left edge)
        task_prio_bars[i] = lv_obj_create(tasks_panel);
        lv_obj_remove_style_all(task_prio_bars[i]);
        lv_obj_set_size(task_prio_bars[i], 4, 24);
        lv_obj_set_pos(task_prio_bars[i], 15, y + 2);
        lv_obj_set_style_bg_color(task_prio_bars[i], COLOR_TEXT_DIM, 0);
        lv_obj_set_style_bg_opa(task_prio_bars[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(task_prio_bars[i], 2, 0);
        lv_obj_add_flag(task_prio_bars[i], LV_OBJ_FLAG_HIDDEN);

        // Task text
        lbl_task_lines[i] = lv_label_create(tasks_panel);
        lv_obj_set_style_text_color(lbl_task_lines[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_task_lines[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_width(lbl_task_lines[i], 460);
        lv_label_set_long_mode(lbl_task_lines[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(lbl_task_lines[i], "");
        lv_obj_set_pos(lbl_task_lines[i], 28, y);
        lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_no_tasks = lv_label_create(tasks_panel);
    lv_obj_set_style_text_color(lbl_no_tasks, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_tasks, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_no_tasks, "Нет задач");
    lv_obj_set_pos(lbl_no_tasks, 15, 38);

    // ===== BOTTOM RIGHT: News (505x305) =====
    lv_obj_t *news_panel = make_card(tile, 514, 285, 505, 305);

    lv_obj_t *lbl_ntitle = lv_label_create(news_panel);
    lv_obj_set_style_text_color(lbl_ntitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_ntitle, &font_montserrat_24_cyr, 0);
    lv_label_set_text(lbl_ntitle, "Новости");
    lv_obj_set_pos(lbl_ntitle, 15, 10);

    for (int i = 0; i < MAX_NEWS_LINES; i++) {
        int y = 38 + i * 46;

        // Category dot
        news_dots[i] = lv_obj_create(news_panel);
        lv_obj_remove_style_all(news_dots[i]);
        lv_obj_set_size(news_dots[i], 8, 8);
        lv_obj_set_pos(news_dots[i], 15, y + 5);
        lv_obj_set_style_bg_color(news_dots[i], COLOR_HIGHLIGHT, 0);
        lv_obj_set_style_bg_opa(news_dots[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(news_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_add_flag(news_dots[i], LV_OBJ_FLAG_HIDDEN);

        // News title
        lbl_news_lines[i] = lv_label_create(news_panel);
        lv_obj_set_style_text_color(lbl_news_lines[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_news_lines[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_width(lbl_news_lines[i], 460);
        lv_label_set_long_mode(lbl_news_lines[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(lbl_news_lines[i], "");
        lv_obj_set_pos(lbl_news_lines[i], 30, y);
        lv_obj_add_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);

        // Category label (below title)
        lbl_news_age[i] = lv_label_create(news_panel);
        lv_obj_set_style_text_color(lbl_news_age[i], COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(lbl_news_age[i], &font_montserrat_16_cyr, 0);
        lv_label_set_text(lbl_news_age[i], "");
        lv_obj_set_pos(lbl_news_age[i], 30, y + 22);
        lv_obj_add_flag(lbl_news_age[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_no_news = lv_label_create(news_panel);
    lv_obj_set_style_text_color(lbl_no_news, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_news, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_no_news, "Нет новостей");
    lv_obj_set_pos(lbl_no_news, 15, 38);
}

// ===== BRIDGE UPDATE =====

static lv_color_t readiness_color(int score)
{
    if (score >= 80) return COLOR_GOOD;
    if (score >= 50) return COLOR_WARN;
    return COLOR_BAD;
}

static void set_metric_card(int idx, const char *value, const char *trend, lv_color_t trend_color)
{
    if (idx >= NUM_METRIC_CARDS || !metric_cards[idx].lbl_value) return;
    lv_label_set_text(metric_cards[idx].lbl_value, value);
    lv_label_set_text(metric_cards[idx].lbl_trend, trend);
    lv_obj_set_style_text_color(metric_cards[idx].lbl_trend, trend_color, 0);
}

static void format_trend(char *buf, int bufsize, int cur, int prev)
{
    if (prev == 0) { buf[0] = '\0'; return; }
    int pct = (int)(((float)(cur - prev) / prev) * 100);
    const char *arrow = pct > 0 ? LV_SYMBOL_UP : (pct < 0 ? LV_SYMBOL_DOWN : "");
    snprintf(buf, bufsize, "%s %d%%", arrow, pct > 0 ? pct : -pct);
}

void ui_dashboard_update_bridge(const bridge_data_t *data)
{
    if (!data) return;

    // Health — readiness arc + metric cards
    if (data->health.valid) {
        int r = data->health.readiness;

        // Arc
        if (arc_readiness) {
            lv_arc_set_value(arc_readiness, r);
            lv_obj_set_style_arc_color(arc_readiness, readiness_color(r), LV_PART_INDICATOR);
        }
        if (lbl_readiness_val) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", r);
            lv_label_set_text(lbl_readiness_val, buf);
            lv_obj_set_style_text_color(lbl_readiness_val, readiness_color(r), 0);
        }
        if (lbl_readiness_label) {
            lv_label_set_text(lbl_readiness_label,
                r >= 80 ? "Отлично" : r >= 50 ? "Норма" : "Низко");
        }

        // Metric cards: 0=Steps, 1=Sleep, 2=Calories, 3=HR, 4=RHR, 5=HRV, 6=SpO2
        char val[16], trend[24];

        // Steps
        snprintf(val, sizeof(val), "%d", data->health.steps);
        format_trend(trend, sizeof(trend), data->health.steps, data->health.steps_prev);
        set_metric_card(0, val, trend, data->health.steps >= data->health.steps_prev ? COLOR_GOOD : COLOR_BAD);

        // Sleep
        snprintf(val, sizeof(val), "%.1fh", data->health.sleep);
        int s10 = (int)(data->health.sleep * 10), sp10 = (int)(data->health.sleep_prev * 10);
        format_trend(trend, sizeof(trend), s10, sp10);
        set_metric_card(1, val, trend, data->health.sleep >= 7.0 ? COLOR_GOOD : (data->health.sleep >= 6.0 ? COLOR_WARN : COLOR_BAD));

        // Calories
        snprintf(val, sizeof(val), "%d", data->health.cal);
        format_trend(trend, sizeof(trend), data->health.cal, data->health.cal_prev);
        set_metric_card(2, val, trend, data->health.cal >= data->health.cal_prev ? COLOR_GOOD : COLOR_TEXT_DIM);

        // Heart Rate (live)
        snprintf(val, sizeof(val), "%d bpm", data->health.hr);
        set_metric_card(3, val, "", data->health.hr <= 100 ? COLOR_GOOD : COLOR_WARN);

        // RHR
        snprintf(val, sizeof(val), "%d bpm", data->health.rhr);
        set_metric_card(4, val, "", data->health.rhr <= 60 ? COLOR_GOOD : (data->health.rhr <= 75 ? COLOR_WARN : COLOR_BAD));

        // HRV
        snprintf(val, sizeof(val), "%d ms", data->health.hrv);
        set_metric_card(5, val, "", data->health.hrv >= 40 ? COLOR_GOOD : (data->health.hrv >= 25 ? COLOR_WARN : COLOR_BAD));

        // SpO2
        snprintf(val, sizeof(val), "%d%%", data->health.spo2);
        set_metric_card(6, val, "", data->health.spo2 >= 95 ? COLOR_GOOD : COLOR_WARN);
    }

    // Tasks — with priority bars
    if (data->tasks_valid) {
        int n = data->task_count;
        if (n == 0) {
            if (lbl_no_tasks) lv_obj_clear_flag(lbl_no_tasks, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (lbl_no_tasks) lv_obj_add_flag(lbl_no_tasks, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < MAX_TASK_LINES; i++) {
            if (i < n) {
                // Priority bar color
                lv_color_t pcolor = data->tasks[i].priority >= 4 ? COLOR_BAD :
                                    data->tasks[i].priority >= 3 ? COLOR_WARN : COLOR_TEXT_DIM;
                if (task_prio_bars[i]) {
                    lv_obj_set_style_bg_color(task_prio_bars[i], pcolor, 0);
                    lv_obj_clear_flag(task_prio_bars[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (lbl_task_lines[i]) {
                    lv_label_set_text(lbl_task_lines[i], data->tasks[i].title);
                    lv_obj_clear_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (task_prio_bars[i]) lv_obj_add_flag(task_prio_bars[i], LV_OBJ_FLAG_HIDDEN);
                if (lbl_task_lines[i]) lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // News — with dots and age
    if (data->news_valid) {
        int n = data->news_count;
        if (n == 0) {
            if (lbl_no_news) lv_obj_clear_flag(lbl_no_news, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (lbl_no_news) lv_obj_add_flag(lbl_no_news, LV_OBJ_FLAG_HIDDEN);
        }

        // Category colors
        static const lv_color_t cat_colors[] = {
            {.full = 0x53A8}, {.full = 0x66BB}, {.full = 0xFFA7},
            {.full = 0xAB47}, {.full = 0xEF53}
        };

        for (int i = 0; i < MAX_NEWS_LINES; i++) {
            if (i < n) {
                if (news_dots[i]) {
                    // Simple hash for category color
                    int cidx = (data->news[i].category[0] + data->news[i].category[1]) % 5;
                    lv_obj_set_style_bg_color(news_dots[i],
                        lv_color_hex(cidx == 0 ? 0x5BC0EB : cidx == 1 ? 0x66BB6A :
                                     cidx == 2 ? 0xFFA726 : cidx == 3 ? 0xAB47BC : 0xEF5350), 0);
                    lv_obj_clear_flag(news_dots[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (lbl_news_lines[i]) {
                    char news_buf[160];
                    lv_label_set_text(lbl_news_lines[i], data->news[i].title);
                    lv_obj_clear_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (lbl_news_age[i]) {
                    lv_label_set_text(lbl_news_age[i], data->news[i].category);
                    lv_obj_clear_flag(lbl_news_age[i], LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                if (news_dots[i]) lv_obj_add_flag(news_dots[i], LV_OBJ_FLAG_HIDDEN);
                if (lbl_news_lines[i]) lv_obj_add_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
                if (lbl_news_age[i]) lv_obj_add_flag(lbl_news_age[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// ===== PAGE 3: HA Control — rooms =====

#define COLOR_LIGHT_ON  lv_color_hex(0xFFC107)
#define COLOR_LIGHT_OFF lv_color_hex(0x3A3A5C)

// Encode room_idx and light_idx into one int for event callback
#define LIGHT_CB_ID(room, light) ((room) * MAX_ROOM_LIGHTS + (light))
#define LIGHT_CB_ROOM(id)        ((id) / MAX_ROOM_LIGHTS)
#define LIGHT_CB_LIGHT(id)       ((id) % MAX_ROOM_LIGHTS)

static void light_btn_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    int room = LIGHT_CB_ROOM(id);
    int light = LIGHT_CB_LIGHT(id);
    if (room < MAX_ROOMS && light < ROOMS[room].light_count) {
        request_light_toggle(ROOMS[room].light_ids[light]);
    }
}

static void create_page3(lv_obj_t *tile)
{
    // Title
    lv_obj_t *pg_title = lv_label_create(tile);
    lv_obj_set_style_text_color(pg_title, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(pg_title, &font_montserrat_24_cyr, 0);
    lv_label_set_text(pg_title, "Дом");
    lv_obj_set_pos(pg_title, 15, 8);

    // 2x2 grid of room cards
    int pw = 500, ph = 265;
    int positions[MAX_ROOMS][2] = {
        {5, 40}, {512, 40}, {5, 310}, {512, 310}
    };

    for (int r = 0; r < MAX_ROOMS; r++) {
        const room_def_t *room = &ROOMS[r];
        lv_obj_t *panel = make_card(tile, positions[r][0], positions[r][1], pw, ph);

        // Room title
        lv_obj_t *lbl_title = lv_label_create(panel);
        lv_obj_set_style_text_color(lbl_title, COLOR_HIGHLIGHT, 0);
        lv_obj_set_style_text_font(lbl_title, &font_montserrat_24_cyr, 0);
        lv_label_set_text(lbl_title, room->name);
        lv_obj_set_pos(lbl_title, 15, 8);

        // Light toggle buttons
        for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
            lv_obj_t *btn = lv_btn_create(panel);
            lv_obj_set_size(btn, 148, 44);
            lv_obj_set_pos(btn, 15 + l * 156, 40);
            lv_obj_set_style_bg_color(btn, COLOR_LIGHT_OFF, 0);
            lv_obj_set_style_radius(btn, 22, 0);  // pill shape
            lv_obj_set_style_pad_all(btn, 4, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, light_btn_cb, LV_EVENT_CLICKED,
                                (void *)(intptr_t)LIGHT_CB_ID(r, l));

            lv_obj_t *lbl = lv_label_create(btn);
            lv_obj_set_style_text_color(lbl, COLOR_TEXT, 0);
            lv_obj_set_style_text_font(lbl, &font_montserrat_16_cyr, 0);
            lv_obj_set_width(lbl, 132);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(lbl, "---");
            lv_obj_center(lbl);

            room_light_btns[r][l] = btn;
            room_light_labels[r][l] = lbl;

            if (l >= room->light_count) {
                lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
            }
        }

        // Sensor cards (below lights, like Health metric cards)
        int sc_w = 140, sc_h = 68, sc_gap = 6;
        int sc_per_row = (pw - 20) / (sc_w + sc_gap);
        for (int s = 0; s < MAX_ROOM_SENSORS; s++) {
            int col = s % sc_per_row;
            int row = s / sc_per_row;
            int sx = 12 + col * (sc_w + sc_gap);
            int sy = 90 + row * (sc_h + sc_gap);

            lv_obj_t *sc = make_card(panel, sx, sy, sc_w, sc_h);

            // Value
            lv_obj_t *vlbl = lv_label_create(sc);
            lv_obj_set_style_text_color(vlbl, COLOR_TEXT, 0);
            lv_obj_set_style_text_font(vlbl, &font_montserrat_24_cyr, 0);
            lv_obj_set_width(vlbl, sc_w - 12);
            lv_obj_set_style_text_align(vlbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_text(vlbl, "---");
            lv_obj_set_pos(vlbl, 6, 4);

            // Name
            lv_obj_t *nlbl = lv_label_create(sc);
            lv_obj_set_style_text_color(nlbl, COLOR_TEXT_DIM, 0);
            lv_obj_set_style_text_font(nlbl, &font_montserrat_16_cyr, 0);
            lv_obj_set_width(nlbl, sc_w - 12);
            lv_obj_set_style_text_align(nlbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(nlbl, LV_LABEL_LONG_SCROLL_CIRCULAR);
            lv_label_set_text(nlbl, "");
            lv_obj_set_pos(nlbl, 6, 38);

            room_sensor_cards[r][s] = sc;
            room_sensor_val_lbl[r][s] = vlbl;
            room_sensor_name_lbl[r][s] = nlbl;

            if (s >= room->sensor_count) {
                lv_obj_add_flag(sc, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

// Find light/sensor in bridge data by entity_id
static const bridge_light_t *find_light(const bridge_data_t *d, const char *eid)
{
    for (int i = 0; i < d->light_count; i++) {
        if (strcmp(d->lights[i].entity_id, eid) == 0) return &d->lights[i];
    }
    return NULL;
}

void ui_dashboard_update_ha(const bridge_data_t *data)
{
    if (!data) return;

    // Update lights per room
    if (data->lights_valid) {
        for (int r = 0; r < MAX_ROOMS; r++) {
            const room_def_t *room = &ROOMS[r];
            for (int l = 0; l < room->light_count; l++) {
                const bridge_light_t *light = find_light(data, room->light_ids[l]);
                if (!light || !room_light_btns[r][l]) continue;

                lv_label_set_text(room_light_labels[r][l], light->name);

                lv_obj_set_style_bg_color(room_light_btns[r][l],
                    light->on ? COLOR_LIGHT_ON : COLOR_LIGHT_OFF, 0);
                lv_obj_set_style_text_color(room_light_labels[r][l],
                    light->on ? lv_color_hex(0x1A1A2E) : COLOR_TEXT, 0);
            }
        }
    }

    // Update sensor tables per room
    if (data->sensors_valid) {
        static const char *sensor_entity_order[] = {
            "sensor.gostinaia_airq_co2",
            "sensor.co2_sensor_co2",
            "sensor.zhimi_vb4_f663_pm25_density",
            "sensor.zhimi_ca4_90f5_relative_humidity_2",
            "sensor.purifier_humidifier_humidity",
            "sensor.purifier_humidifier_temperature",
            "sensor.zhimi_vb4_f663_temperature",
            "sensor.aqara_sensor_temperature",
            "sensor.aqara_sensor_humidity",
        };
        int sensor_order_count = sizeof(sensor_entity_order) / sizeof(sensor_entity_order[0]);

        // Entity-specific short names
        struct sensor_label_t { const char *entity; const char *label; };
        static const sensor_label_t sensor_labels[] = {
            {"sensor.gostinaia_airq_co2",              "CO2"},
            {"sensor.co2_sensor_co2",                  "CO2"},
            {"sensor.zhimi_vb4_f663_pm25_density",     "PM2.5"},
            {"sensor.zhimi_ca4_90f5_relative_humidity_2", "Влажн."},
            {"sensor.purifier_humidifier_humidity",     "Влажн."},
            {"sensor.purifier_humidifier_temperature",  "Темп."},
            {"sensor.zhimi_vb4_f663_temperature",       "Темп.(очист.)"},
            {"sensor.zhimi_vb4_f663_relative_humidity",  "Влажн.(очист.)"},
            {"sensor.aqara_sensor_temperature",         "Темп.(Aqara)"},
            {"sensor.aqara_sensor_humidity",            "Влажн.(Aqara)"},
        };
        int num_labels = sizeof(sensor_labels) / sizeof(sensor_labels[0]);

        for (int r = 0; r < MAX_ROOMS; r++) {
            const room_def_t *room = &ROOMS[r];
            if (!room_sensor_val_lbl[r][0]) continue;

            for (int s = 0; s < room->sensor_count; s++) {
                const char *wanted = room->sensor_ids[s];
                if (!wanted || !room_sensor_cards[r][s]) continue;

                bool found = false;
                for (int k = 0; k < sensor_order_count && k < data->sensor_count; k++) {
                    if (strcmp(sensor_entity_order[k], wanted) == 0) {
                        const bridge_sensor_t *sen = &data->sensors[k];

                        // Skip unavailable sensors
                        if (sen->value[0] == '\0' || strcmp(sen->value, "0") == 0) break;

                        // Find entity-specific label
                        const char *label = sen->name;
                        for (int n = 0; n < num_labels; n++) {
                            if (strcmp(sensor_labels[n].entity, wanted) == 0) {
                                label = sensor_labels[n].label;
                                break;
                            }
                        }

                        lv_label_set_text(room_sensor_val_lbl[r][s], sen->value);
                        lv_label_set_text(room_sensor_name_lbl[r][s], label);
                        lv_obj_clear_flag(room_sensor_cards[r][s], LV_OBJ_FLAG_HIDDEN);

                        // CO2 color coding
                        if (strstr(sen->unit, "ppm")) {
                            int co2 = atoi(sen->value);
                            lv_color_t co2_color = co2 < 800 ? COLOR_GOOD :
                                                   co2 < 1000 ? COLOR_WARN : COLOR_BAD;
                            lv_obj_set_style_text_color(room_sensor_val_lbl[r][s], co2_color, 0);
                        }
                        found = true;
                        break;
                    }
                }
                // Hide card if sensor not found in bridge data
                if (!found) {
                    lv_obj_add_flag(room_sensor_cards[r][s], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }
    }
}
