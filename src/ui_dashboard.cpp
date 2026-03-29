#include "ui_dashboard.h"
#include "config.h"
#include "wifi_manager.h"
#include "weather.h"
#include "ha_calendar.h"
#include "transport.h"
#include "bridge.h"
#include "weather_icons.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <time.h>
#include <stdio.h>

// Declared in main.cpp
extern void request_calendar_date(int year, int month, int day);

// Custom font with Cyrillic support (for HA calendar events)
LV_FONT_DECLARE(font_montserrat_16_cyr);

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

// Page 2: Health + Tasks + News
static lv_obj_t *lbl_health_steps = NULL;
static lv_obj_t *lbl_health_sleep = NULL;
static lv_obj_t *lbl_health_hr    = NULL;
static lv_obj_t *lbl_health_ready = NULL;
#define MAX_TASK_LINES 8
static lv_obj_t *lbl_task_lines[MAX_TASK_LINES] = {};
static lv_obj_t *lbl_no_tasks = NULL;
#define MAX_NEWS_LINES 5
static lv_obj_t *lbl_news_lines[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_no_news = NULL;
// Page indicator dots
static lv_obj_t *dot_indicators[2] = {};

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

static const char *DOW_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTH_NAMES[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

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
    const weather_data_t *w = weather_get_data();
    ui_dashboard_update_weather(w);
}

static void timer_ha_cal_cb(lv_timer_t *timer)
{
    (void)timer;
    const ha_cal_data_t *d = ha_calendar_get_data();
    ui_dashboard_update_ha_calendar(d);
}

static void timer_transport_cb(lv_timer_t *timer)
{
    (void)timer;
    const transport_data_t *t = transport_get_data();
    ui_dashboard_update_transport(t);
}

static void timer_bridge_cb(lv_timer_t *timer)
{
    (void)timer;
    const bridge_data_t *d = bridge_get_data();
    ui_dashboard_update_bridge(d);
}

static void update_dot_indicators(int active)
{
    for (int i = 0; i < 2; i++) {
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
        lv_label_set_text(lbl_sched_title, "Today's Schedule");
        if (btn_today) lv_obj_add_flag(btn_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        char buf[48];
        const char *mon = (m >= 1 && m <= 12) ? MONTH_NAMES[m - 1] : "???";
        snprintf(buf, sizeof(buf), "Schedule for %s %d", mon, d);
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
    lv_obj_t *tile2 = lv_tileview_add_tile(tileview, 1, 0, LV_DIR_LEFT);

    // Page indicator dots (bottom center, above bottom bar)
    for (int i = 0; i < 2; i++) {
        dot_indicators[i] = lv_obj_create(scr);
        lv_obj_remove_style_all(dot_indicators[i]);
        lv_obj_set_size(dot_indicators[i], 8, 8);
        lv_obj_set_pos(dot_indicators[i], 504 + i * 16, 572);
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
    lv_obj_set_style_text_font(lbl_datetime, &lv_font_montserrat_24, 0);
    lv_obj_set_width(lbl_datetime, 500);
    lv_label_set_long_mode(lbl_datetime, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_datetime, "...");
    lv_obj_set_pos(lbl_datetime, 15, 15);

    lbl_topbar_temp = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_topbar_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_topbar_temp, &lv_font_montserrat_24, 0);
    lv_obj_set_width(lbl_topbar_temp, 400);
    lv_label_set_long_mode(lbl_topbar_temp, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_topbar_temp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_topbar_temp, WEATHER_CITY);
    lv_obj_set_pos(lbl_topbar_temp, 609, 15);

    // ---- LEFT PANEL (weather + calendar) ----
    lv_obj_t *cal_panel = lv_obj_create(page);
    lv_obj_remove_style_all(cal_panel);
    lv_obj_set_size(cal_panel, 410, 510);
    lv_obj_set_pos(cal_panel, 5, 65);
    lv_obj_set_style_bg_color(cal_panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(cal_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cal_panel, 8, 0);
    lv_obj_clear_flag(cal_panel, LV_OBJ_FLAG_SCROLLABLE);

    // Weather icon canvas (48x48)
    weather_canvas = lv_canvas_create(cal_panel);
    canvas_buf = (lv_color_t *)heap_caps_malloc(ICON_SIZE * ICON_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (canvas_buf) {
        lv_canvas_set_buffer(weather_canvas, canvas_buf, ICON_SIZE, ICON_SIZE, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(weather_canvas, COLOR_PANEL, LV_OPA_COVER);
    }
    lv_obj_set_pos(weather_canvas, 10, 8);

    // Weather main label: "Clear  5C"
    lbl_weather_main = lv_label_create(cal_panel);
    lv_obj_set_style_text_color(lbl_weather_main, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_weather_main, &lv_font_montserrat_24, 0);
    lv_obj_set_width(lbl_weather_main, 320);
    lv_label_set_long_mode(lbl_weather_main, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_weather_main, "...");
    lv_obj_set_pos(lbl_weather_main, 70, 8);

    // Weather detail label: "H:78%  W:12km/h  Tmrw: 8/2C"
    lbl_weather_detail = lv_label_create(cal_panel);
    lv_obj_set_style_text_color(lbl_weather_detail, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_weather_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_weather_detail, 320);
    lv_label_set_long_mode(lbl_weather_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_weather_detail, "");
    lv_obj_set_pos(lbl_weather_detail, 70, 38);

    // Separator below weather
    lv_obj_t *weather_sep = lv_obj_create(cal_panel);
    lv_obj_remove_style_all(weather_sep);
    lv_obj_set_size(weather_sep, 390, 2);
    lv_obj_set_pos(weather_sep, 10, 62);
    lv_obj_set_style_bg_color(weather_sep, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(weather_sep, LV_OPA_COVER, 0);
    lv_obj_clear_flag(weather_sep, LV_OBJ_FLAG_SCROLLABLE);

    // Calendar widget (below weather)
    calendar = lv_calendar_create(cal_panel);
    lv_obj_set_size(calendar, 390, 430);
    lv_obj_align(calendar, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(calendar, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(calendar, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(calendar, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(calendar, &lv_font_montserrat_16, 0);
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
    lv_obj_set_style_text_font(lbl_sched_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_sched_title, "Today's Schedule");
    lv_obj_set_pos(lbl_sched_title, 20, 10);

    // "Today" button
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
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_label_set_text(btn_lbl, "Today");
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
    lv_label_set_text(lbl_no_events, "No events today");

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
    lv_obj_set_style_text_font(lbl_tr_title, &lv_font_montserrat_16, 0);
    lv_label_set_text(lbl_tr_title, "Djeram");
    lv_obj_set_pos(lbl_tr_title, 20, 345);

    // Outbound (stop 89)
    lv_obj_t *lbl_out_dir = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_out_dir, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_out_dir, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_out_dir, LV_SYMBOL_RIGHT " Oblast");
    lv_obj_set_pos(lbl_out_dir, 20, 375);

    lbl_transport_out = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_transport_out, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_transport_out, &lv_font_montserrat_16, 0);
    lv_obj_set_width(lbl_transport_out, 440);
    lv_label_set_long_mode(lbl_transport_out, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_recolor(lbl_transport_out, true);
    lv_label_set_text(lbl_transport_out, "---");
    lv_obj_set_pos(lbl_transport_out, 130, 373);

    // Inbound (stop 90)
    lv_obj_t *lbl_in_dir = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_in_dir, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_in_dir, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_in_dir, LV_SYMBOL_LEFT " Centar");
    lv_obj_set_pos(lbl_in_dir, 20, 405);

    lbl_transport_in = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_transport_in, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_transport_in, &lv_font_montserrat_16, 0);
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
    lv_obj_set_style_text_font(lbl_bottom, &lv_font_montserrat_12, 0);
    lv_obj_set_width(lbl_bottom, 994);
    lv_label_set_long_mode(lbl_bottom, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_bottom, "WiFi: ---");
    lv_obj_set_pos(lbl_bottom, 10, 2);

    // ===== PAGE 2: Health + Tasks + News =====
    create_page2(tile2);

    // Timers
    lv_timer_create(timer_time_cb, 5000, NULL);
    lv_timer_create(timer_weather_cb, 10000, NULL);
    lv_timer_create(timer_ha_cal_cb, 5000, NULL);
    lv_timer_create(timer_transport_cb, 5000, NULL);
    lv_timer_create(timer_bridge_cb, 5000, NULL);

    // Initial update
    ui_dashboard_update_time();
}

void ui_dashboard_update_weather(const weather_data_t *data)
{
    if (!data || !data->valid) return;

    // Update weather icon (only redraw if code changed)
    if (weather_canvas && canvas_buf && data->weather_code != last_weather_code) {
        weather_icon_draw(weather_canvas, data->weather_code);
        last_weather_code = data->weather_code;
    }

    // Main line: "Clear  5C"
    if (lbl_weather_main) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s  %.0fC",
                 weather_code_to_text(data->weather_code),
                 data->temp);
        lv_label_set_text(lbl_weather_main, buf);
    }

    // Detail line: "H:78%  W:12km/h  Tmrw: 8/2C"
    if (lbl_weather_detail) {
        char buf[96];
        snprintf(buf, sizeof(buf), "H:%.0f%%  W:%.0fkm/h  Tmrw: %.0f/%.0fC",
                 data->humidity,
                 data->wind_speed,
                 data->daily[1].temp_max,
                 data->daily[1].temp_min);
        lv_label_set_text(lbl_weather_detail, buf);
    }

    // Top bar temp
    if (lbl_topbar_temp) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%s  %.0fC", WEATHER_CITY, data->temp);
        lv_label_set_text(lbl_topbar_temp, buf);
    }
}

void ui_dashboard_update_ha_calendar(const ha_cal_data_t *data)
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
                lv_label_set_text(lbl_no_events, "No events today");
            } else {
                char buf[48];
                const char *mon = (sel_month >= 1 && sel_month <= 12) ? MONTH_NAMES[sel_month - 1] : "???";
                snprintf(buf, sizeof(buf), "No events on %s %d", mon, sel_day);
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
            const ha_cal_event_t *ev = &data->events[i];
            char buf[96];
            if (ev->all_day) {
                snprintf(buf, sizeof(buf), "All day      %s", ev->summary);
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
            const ha_cal_event_t *ev = &data->events[i];
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

static void format_stop_line(char *buf, int buf_size, const transport_stop_t *stop)
{
    if (stop->count == 0) {
        snprintf(buf, buf_size, "no data");
        return;
    }
    int pos = 0;
    for (int i = 0; i < stop->count && pos < buf_size - 1; i++) {
        const transport_vehicle_t *v = &stop->vehicles[i];
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

void ui_dashboard_update_transport(const transport_data_t *data)
{
    if (!data || !data->valid) return;

    if (lbl_transport_out) {
        char buf[256];
        format_stop_line(buf, sizeof(buf), &data->stops[TRANSPORT_STOP_OUT]);
        lv_label_set_text(lbl_transport_out, buf);
    }

    if (lbl_transport_in) {
        char buf[256];
        format_stop_line(buf, sizeof(buf), &data->stops[TRANSPORT_STOP_IN]);
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
        snprintf(buf, sizeof(buf), "WiFi: %s  Heap: %.1fKB  W: %s  Cal: %s  Tr: %s  Br: %s",
                 wifi_is_connected() ? "OK" : "---",
                 esp_get_free_heap_size() / 1024.0f,
                 weather_get_last_error(),
                 ha_calendar_get_last_error(),
                 transport_get_last_error(),
                 bridge_get_last_error());
        lv_label_set_text(lbl_bottom, buf);
    }
}

// ===== PAGE 2: Health + Tasks + News =====

static void create_page2(lv_obj_t *tile)
{
    // ---- HEALTH PANEL (left, 410x560) ----
    lv_obj_t *health_panel = lv_obj_create(tile);
    lv_obj_remove_style_all(health_panel);
    lv_obj_set_size(health_panel, 410, 560);
    lv_obj_set_pos(health_panel, 5, 5);
    lv_obj_set_style_bg_color(health_panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(health_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(health_panel, 8, 0);
    lv_obj_clear_flag(health_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_htitle = lv_label_create(health_panel);
    lv_obj_set_style_text_color(lbl_htitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_htitle, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_htitle, LV_SYMBOL_CHARGE " Health");
    lv_obj_set_pos(lbl_htitle, 20, 10);

    // Readiness score (big)
    lbl_health_ready = lv_label_create(health_panel);
    lv_obj_set_style_text_color(lbl_health_ready, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_health_ready, &lv_font_montserrat_24, 0);
    lv_label_set_text(lbl_health_ready, "Readiness: ---");
    lv_obj_set_pos(lbl_health_ready, 20, 50);

    // Steps
    lbl_health_steps = lv_label_create(health_panel);
    lv_obj_set_style_text_color(lbl_health_steps, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_health_steps, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_health_steps, LV_SYMBOL_REFRESH " Steps: ---");
    lv_obj_set_pos(lbl_health_steps, 20, 100);

    // Sleep
    lbl_health_sleep = lv_label_create(health_panel);
    lv_obj_set_style_text_color(lbl_health_sleep, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_health_sleep, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_health_sleep, "Sleep: ---");
    lv_obj_set_pos(lbl_health_sleep, 20, 145);

    // Heart rate
    lbl_health_hr = lv_label_create(health_panel);
    lv_obj_set_style_text_color(lbl_health_hr, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_health_hr, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_health_hr, "HR: ---");
    lv_obj_set_pos(lbl_health_hr, 20, 190);

    // ---- TASKS PANEL (right top, 599x270) ----
    lv_obj_t *tasks_panel = lv_obj_create(tile);
    lv_obj_remove_style_all(tasks_panel);
    lv_obj_set_size(tasks_panel, 599, 270);
    lv_obj_set_pos(tasks_panel, 420, 5);
    lv_obj_set_style_bg_color(tasks_panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(tasks_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tasks_panel, 8, 0);
    lv_obj_clear_flag(tasks_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_ttitle = lv_label_create(tasks_panel);
    lv_obj_set_style_text_color(lbl_ttitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_ttitle, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_ttitle, LV_SYMBOL_LIST " Tasks");
    lv_obj_set_pos(lbl_ttitle, 20, 10);

    for (int i = 0; i < MAX_TASK_LINES; i++) {
        lbl_task_lines[i] = lv_label_create(tasks_panel);
        lv_obj_set_style_text_color(lbl_task_lines[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_task_lines[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_width(lbl_task_lines[i], 559);
        lv_label_set_long_mode(lbl_task_lines[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(lbl_task_lines[i], "");
        lv_obj_set_pos(lbl_task_lines[i], 20, 42 + i * 28);
        lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_no_tasks = lv_label_create(tasks_panel);
    lv_obj_set_style_text_color(lbl_no_tasks, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_tasks, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_no_tasks, "No tasks");
    lv_obj_set_pos(lbl_no_tasks, 20, 42);

    // ---- NEWS PANEL (right bottom, 599x285) ----
    lv_obj_t *news_panel = lv_obj_create(tile);
    lv_obj_remove_style_all(news_panel);
    lv_obj_set_size(news_panel, 599, 285);
    lv_obj_set_pos(news_panel, 420, 280);
    lv_obj_set_style_bg_color(news_panel, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(news_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(news_panel, 8, 0);
    lv_obj_clear_flag(news_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_ntitle = lv_label_create(news_panel);
    lv_obj_set_style_text_color(lbl_ntitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_ntitle, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_ntitle, LV_SYMBOL_FILE " News");
    lv_obj_set_pos(lbl_ntitle, 20, 10);

    for (int i = 0; i < MAX_NEWS_LINES; i++) {
        lbl_news_lines[i] = lv_label_create(news_panel);
        lv_obj_set_style_text_color(lbl_news_lines[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_news_lines[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_width(lbl_news_lines[i], 559);
        lv_label_set_long_mode(lbl_news_lines[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(lbl_news_lines[i], "");
        lv_obj_set_pos(lbl_news_lines[i], 20, 42 + i * 45);
        lv_obj_add_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_no_news = lv_label_create(news_panel);
    lv_obj_set_style_text_color(lbl_no_news, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_news, &lv_font_montserrat_14, 0);
    lv_label_set_text(lbl_no_news, "No news");
    lv_obj_set_pos(lbl_no_news, 20, 42);
}

// ===== BRIDGE UPDATE =====

static const char *trend_arrow(int cur, int prev)
{
    if (cur > prev) return LV_SYMBOL_UP;
    if (cur < prev) return LV_SYMBOL_DOWN;
    return "=";
}

void ui_dashboard_update_bridge(const bridge_data_t *data)
{
    if (!data) return;

    // Health
    if (data->health.valid) {
        if (lbl_health_ready) {
            char buf[48];
            snprintf(buf, sizeof(buf), "Readiness: %d", data->health.readiness);
            lv_label_set_text(lbl_health_ready, buf);
            lv_obj_set_style_text_color(lbl_health_ready,
                data->health.readiness >= 70 ? lv_color_hex(0x66BB6A) :
                data->health.readiness >= 40 ? lv_color_hex(0xFFC107) :
                                               lv_color_hex(0xEF5350), 0);
        }
        if (lbl_health_steps) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%s Steps: %d  %s (prev %d)",
                     LV_SYMBOL_REFRESH, data->health.steps,
                     trend_arrow(data->health.steps, data->health.steps_prev),
                     data->health.steps_prev);
            lv_label_set_text(lbl_health_steps, buf);
        }
        if (lbl_health_sleep) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Sleep: %.1fh  %s (prev %.1fh)",
                     data->health.sleep,
                     trend_arrow((int)(data->health.sleep * 10), (int)(data->health.sleep_prev * 10)),
                     data->health.sleep_prev);
            lv_label_set_text(lbl_health_sleep, buf);
        }
        if (lbl_health_hr) {
            char buf[80];
            snprintf(buf, sizeof(buf), "HR: %d  RHR: %d  HRV: %d  SpO2: %d%%",
                     data->health.hr, data->health.rhr,
                     data->health.hrv, data->health.spo2);
            lv_label_set_text(lbl_health_hr, buf);
        }
    }

    // Tasks
    if (data->tasks_valid) {
        int n = data->task_count;
        if (n == 0) {
            if (lbl_no_tasks) lv_obj_clear_flag(lbl_no_tasks, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (lbl_no_tasks) lv_obj_add_flag(lbl_no_tasks, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < MAX_TASK_LINES; i++) {
            if (!lbl_task_lines[i]) continue;
            if (i < n) {
                char buf[100];
                const char *prio_mark = data->tasks[i].priority >= 4 ? "!! " :
                                        data->tasks[i].priority >= 3 ? "!  " : "   ";
                snprintf(buf, sizeof(buf), "%s%s", prio_mark, data->tasks[i].title);
                lv_label_set_text(lbl_task_lines[i], buf);
                lv_obj_set_style_text_color(lbl_task_lines[i],
                    data->tasks[i].priority >= 4 ? lv_color_hex(0xEF5350) :
                    data->tasks[i].priority >= 3 ? lv_color_hex(0xFFA726) :
                                                   COLOR_TEXT, 0);
                lv_obj_clear_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // News
    if (data->news_valid) {
        int n = data->news_count;
        if (n == 0) {
            if (lbl_no_news) lv_obj_clear_flag(lbl_no_news, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (lbl_no_news) lv_obj_add_flag(lbl_no_news, LV_OBJ_FLAG_HIDDEN);
        }
        for (int i = 0; i < MAX_NEWS_LINES; i++) {
            if (!lbl_news_lines[i]) continue;
            if (i < n) {
                char buf[160];
                snprintf(buf, sizeof(buf), "[%s] %s  (%dh ago)",
                         data->news[i].category,
                         data->news[i].title,
                         data->news[i].hours_ago);
                lv_label_set_text(lbl_news_lines[i], buf);
                lv_obj_clear_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}
