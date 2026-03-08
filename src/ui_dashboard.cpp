#include "ui_dashboard.h"
#include "config.h"
#include "wifi_manager.h"
#include "weather.h"
#include "ha_calendar.h"
#include "transport.h"
#include "weather_icons.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <time.h>

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
#define MAX_EVENT_LINES 8
static lv_obj_t *lbl_events[MAX_EVENT_LINES] = {};
static lv_obj_t *lbl_no_events = NULL;
static lv_obj_t *btn_today     = NULL;
static lv_obj_t *now_line      = NULL;   // "current time" indicator line
static lv_obj_t *right_panel_ref = NULL; // for positioning now_line

// Transport panel elements
static lv_obj_t *lbl_transport_out = NULL;   // stop 89 — outbound
static lv_obj_t *lbl_transport_in  = NULL;   // stop 90 — inbound

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

void ui_dashboard_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // ---- TOP BAR (60px) ----
    lv_obj_t *top_bar = lv_obj_create(scr);
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
    lv_label_set_long_mode(lbl_datetime, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_datetime, "...");
    lv_obj_set_pos(lbl_datetime, 15, 15);

    lbl_topbar_temp = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_topbar_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_topbar_temp, &lv_font_montserrat_24, 0);
    lv_obj_set_width(lbl_topbar_temp, 400);
    lv_label_set_long_mode(lbl_topbar_temp, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(lbl_topbar_temp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_topbar_temp, WEATHER_CITY);
    lv_obj_set_pos(lbl_topbar_temp, 609, 15);

    // ---- LEFT PANEL (weather + calendar) ----
    lv_obj_t *cal_panel = lv_obj_create(scr);
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
    lv_label_set_long_mode(lbl_weather_main, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_weather_main, "...");
    lv_obj_set_pos(lbl_weather_main, 70, 8);

    // Weather detail label: "H:78%  W:12km/h  Tmrw: 8/2C"
    lbl_weather_detail = lv_label_create(cal_panel);
    lv_obj_set_style_text_color(lbl_weather_detail, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_weather_detail, &lv_font_montserrat_14, 0);
    lv_obj_set_width(lbl_weather_detail, 320);
    lv_label_set_long_mode(lbl_weather_detail, LV_LABEL_LONG_CLIP);
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
    right_panel_ref = lv_obj_create(scr);
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

    // Event lines (y=42, 8 lines x 35px = up to y=322)
    for (int i = 0; i < MAX_EVENT_LINES; i++) {
        lbl_events[i] = lv_label_create(right_panel);
        lv_obj_set_style_text_color(lbl_events[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl_events[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_width(lbl_events[i], 559);
        lv_label_set_long_mode(lbl_events[i], LV_LABEL_LONG_CLIP);
        lv_label_set_text(lbl_events[i], "");
        lv_obj_set_pos(lbl_events[i], 20, 42 + i * 35);
        lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
    }

    // "No events" fallback
    lbl_no_events = lv_label_create(right_panel);
    lv_obj_set_style_text_color(lbl_no_events, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_events, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_no_events, "No events today");
    lv_obj_set_pos(lbl_no_events, 20, 42);

    // "Now" time indicator line
    now_line = lv_obj_create(right_panel);
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
    lv_label_set_long_mode(lbl_transport_out, LV_LABEL_LONG_CLIP);
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
    lv_label_set_long_mode(lbl_transport_in, LV_LABEL_LONG_CLIP);
    lv_label_set_recolor(lbl_transport_in, true);
    lv_label_set_text(lbl_transport_in, "---");
    lv_obj_set_pos(lbl_transport_in, 130, 403);

    // ---- BOTTOM BAR (20px) ----
    lv_obj_t *bottom_bar = lv_obj_create(scr);
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
    lv_label_set_long_mode(lbl_bottom, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_bottom, "WiFi: ---");
    lv_obj_set_pos(lbl_bottom, 10, 2);

    // Timers
    lv_timer_create(timer_time_cb, 5000, NULL);
    lv_timer_create(timer_weather_cb, 10000, NULL);
    lv_timer_create(timer_ha_cal_cb, 5000, NULL);
    lv_timer_create(timer_transport_cb, 5000, NULL);

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
        } else if (line_before_idx >= 0 && now_line) {
            // Position the "now" line between events
            // Event rows start at y=42, each 35px apart
            int line_y;
            if (line_before_idx == 0) {
                line_y = 42 - 6; // above first event
            } else if (line_before_idx >= display_count) {
                line_y = 42 + (display_count - 1) * 35 + 25; // below last event
            } else {
                // Between event [line_before_idx-1] and [line_before_idx]
                line_y = 42 + line_before_idx * 35 - 6;
            }
            lv_obj_set_pos(now_line, 30, line_y);
            lv_obj_clear_flag(now_line, LV_OBJ_FLAG_HIDDEN);
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
        snprintf(buf, sizeof(buf), "WiFi: %s  Heap: %.1fKB  W: %s  Cal: %s  Tr: %s",
                 wifi_is_connected() ? "OK" : "---",
                 esp_get_free_heap_size() / 1024.0f,
                 weather_get_last_error(),
                 ha_calendar_get_last_error(),
                 transport_get_last_error());
        lv_label_set_text(lbl_bottom, buf);
    }
}
