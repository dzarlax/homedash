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
#include <cstring>

// Declared in main.cpp
extern void request_calendar_date(int year, int month, int day);
extern void request_light_toggle(const char *entity_id);
extern void request_ota_check(void);

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
static lv_obj_t *hero_event_card = NULL;
static lv_obj_t *lbl_hero_status = NULL;
static lv_obj_t *lbl_hero_title  = NULL;
static lv_obj_t *lbl_hero_time   = NULL;
#define MAX_EVENT_LINES 16
static lv_obj_t *lbl_events[MAX_EVENT_LINES] = {};
static lv_obj_t *events_scroll = NULL;  // scrollable container for events
static lv_obj_t *lbl_no_events = NULL;
static lv_obj_t *btn_today     = NULL;
static lv_obj_t *now_line      = NULL;   // "current time" indicator line
static lv_obj_t *right_panel_ref = NULL; // for positioning now_line

// Transport panel elements
static lv_obj_t *lbl_transport_out = NULL;
static lv_obj_t *lbl_transport_in  = NULL;
static lv_obj_t *lbl_air_primary = NULL;
static lv_obj_t *lbl_air_detail = NULL;

// Tileview
static lv_obj_t *tileview = NULL;
static lv_obj_t *nav_tiles[3] = {};
static lv_obj_t *week_day_buttons[7] = {};
static lv_obj_t *week_day_labels[7] = {};
static lv_calendar_date_t week_dates[7] = {};
static lv_obj_t *reader_overlay = NULL;

// bridge_data_t includes bounded Evening News summaries and is larger than the
// LVGL task stack. All LVGL timers run serially, so one static snapshot keeps
// the copy out of that stack without concurrent access.
static bridge_data_t ui_bridge_snapshot = {};

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
static char task_titles[MAX_TASK_LINES][160] = {};
static char task_due[MAX_TASK_LINES][12] = {};

// News
#define MAX_NEWS_LINES 5
static lv_obj_t *news_dots[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_news_lines[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_news_age[MAX_NEWS_LINES] = {};
static lv_obj_t *lbl_no_news = NULL;
static char news_titles[MAX_NEWS_LINES][256] = {};
static char news_summaries[MAX_NEWS_LINES][1536] = {};
static char news_categories[MAX_NEWS_LINES][24] = {};

// Shared light, warm palette.  Status colours are reserved for real status.
#define COLOR_CARD      lv_color_hex(0xFFFFFF)
#define COLOR_GOOD      lv_color_hex(0x4F8B68)
#define COLOR_WARN      lv_color_hex(0xC58B35)
#define COLOR_BAD       lv_color_hex(0xC96A5A)
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

#define COLOR_NOW lv_color_hex(0xC96A5A)

// Track last calendar date to avoid unnecessary updates
static int last_cal_year = 0;
static int last_cal_mon  = 0;
static int last_cal_day  = 0;

// Currently selected date for schedule display
static int sel_year  = 0;
static int sel_month = 0;
static int sel_day   = 0;
static int64_t selected_non_today_at_us = 0;
static lv_calendar_date_t calendar_highlighted_dates[1] = {};

#define CALENDAR_AUTO_RETURN_US (5LL * 60LL * 1000000LL)

// Track last weather code to avoid unnecessary redraws
static int last_weather_code = -1;

static const char *DOW_NAMES[] = {"Bc", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};
static const char *MONTH_NAMES[] = {"Янв", "Фев", "Мар", "Апр", "Май", "Июн",
                                     "Июл", "Авг", "Сен", "Окт", "Ноя", "Дек"};

#define COLOR_BG        lv_color_hex(0xF4F1EA)
#define COLOR_PANEL     lv_color_hex(0xE8EDE6)
#define COLOR_ACCENT    lv_color_hex(0xD7E4DA)
#define COLOR_TEXT      lv_color_hex(0x24352E)
#define COLOR_TEXT_DIM  lv_color_hex(0x6B7970)
#define COLOR_HIGHLIGHT lv_color_hex(0x4F7564)

// Calendar event colors (per calendar index)
#define NUM_CAL_COLORS 6
static lv_color_t CAL_COLORS[NUM_CAL_COLORS];  // initialized at runtime via cal_color()
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

static bool is_today(int y, int m, int d);
static void update_schedule_title(int y, int m, int d);
static void sync_calendar_selection(void);
static void update_week_selection(void);
static void refresh_week_strip(void);

static void get_today_date(int *y, int *m, int *d)
{
    time_t now;
    time(&now);
    struct tm tinfo;
    localtime_r(&now, &tinfo);
    if (y) *y = tinfo.tm_year + 1900;
    if (m) *m = tinfo.tm_mon + 1;
    if (d) *d = tinfo.tm_mday;
}

static void return_calendar_to_today(void)
{
    int y, m, d;
    get_today_date(&y, &m, &d);

    sel_year = y;
    sel_month = m;
    sel_day = d;
    selected_non_today_at_us = 0;

    update_schedule_title(sel_year, sel_month, sel_day);
    update_week_selection();
    if (calendar) lv_calendar_set_showed_date(calendar, sel_year, sel_month);
    sync_calendar_selection();
    request_calendar_date(sel_year, sel_month, sel_day);
}

static void check_calendar_auto_return(void)
{
    if (selected_non_today_at_us == 0) return;
    if (sel_year == 0 || is_today(sel_year, sel_month, sel_day)) {
        selected_non_today_at_us = 0;
        return;
    }

    int64_t elapsed = esp_timer_get_time() - selected_non_today_at_us;
    if (elapsed >= CALENDAR_AUTO_RETURN_US) {
        return_calendar_to_today();
    }
}

static void timer_time_cb(lv_timer_t *timer)
{
    (void)timer;
    check_calendar_auto_return();
    ui_dashboard_update_time();
}

static void timer_weather_cb(lv_timer_t *timer)
{
    (void)timer;
    if (bridge_copy_data(&ui_bridge_snapshot)) ui_dashboard_update_weather(&ui_bridge_snapshot.weather);
}

static void timer_ha_cal_cb(lv_timer_t *timer)
{
    (void)timer;
    bridge_cal_data_t d = {};
    if (bridge_copy_calendar_data(&d)) ui_dashboard_update_ha_calendar(&d);
}

static void timer_transport_cb(lv_timer_t *timer)
{
    (void)timer;
    if (bridge_copy_data(&ui_bridge_snapshot)) ui_dashboard_update_transport(&ui_bridge_snapshot.transport);
}

static void timer_bridge_cb(lv_timer_t *timer)
{
    (void)timer;
    if (bridge_copy_data(&ui_bridge_snapshot)) {
        ui_dashboard_update_bridge(&ui_bridge_snapshot);
        ui_dashboard_update_ha(&ui_bridge_snapshot);
    }
}

static bool is_today(int y, int m, int d)
{
    int ty, tm, td;
    get_today_date(&ty, &tm, &td);
    return (y == ty && m == tm && d == td);
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
    if (lv_calendar_get_pressed_date(calendar, &date) != LV_RESULT_OK) return;

    sel_year  = date.year;
    sel_month = date.month;
    sel_day   = date.day;
    selected_non_today_at_us = is_today(sel_year, sel_month, sel_day) ? 0 : esp_timer_get_time();

    update_schedule_title(sel_year, sel_month, sel_day);
    update_week_selection();
    sync_calendar_selection();
    request_calendar_date(sel_year, sel_month, sel_day);
}

static void btn_today_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    get_today_date(&sel_year, &sel_month, &sel_day);
    selected_non_today_at_us = 0;

    update_schedule_title(sel_year, sel_month, sel_day);
    update_week_selection();
    if (calendar) lv_calendar_set_showed_date(calendar, sel_year, sel_month);
    sync_calendar_selection();
    request_calendar_date(sel_year, sel_month, sel_day);
}

static void week_day_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const lv_calendar_date_t *date = (const lv_calendar_date_t *)lv_event_get_user_data(e);
    if (!date) return;

    sel_year = date->year;
    sel_month = date->month;
    sel_day = date->day;
    selected_non_today_at_us = is_today(sel_year, sel_month, sel_day) ? 0 : esp_timer_get_time();
    update_schedule_title(sel_year, sel_month, sel_day);
    update_week_selection();
    request_calendar_date(sel_year, sel_month, sel_day);
}

static void update_week_selection(void)
{
    for (int i = 0; i < 7; i++) {
        if (!week_day_buttons[i]) continue;
        bool selected = week_dates[i].year == sel_year &&
                        week_dates[i].month == sel_month &&
                        week_dates[i].day == sel_day;
        lv_obj_set_style_bg_color(week_day_buttons[i], selected ? COLOR_ACCENT : COLOR_CARD, 0);
    }
}

static void refresh_week_strip(void)
{
    int y, m, d;
    get_today_date(&y, &m, &d);
    struct tm week_tm = {};
    week_tm.tm_year = y - 1900;
    week_tm.tm_mon = m - 1;
    week_tm.tm_mday = d;
    if (mktime(&week_tm) == (time_t)-1) return;

    for (int i = 0; i < 7; i++) {
        struct tm date_tm = week_tm;
        date_tm.tm_mday += i;
        if (mktime(&date_tm) == (time_t)-1) continue;

        week_dates[i].year = date_tm.tm_year + 1900;
        week_dates[i].month = date_tm.tm_mon + 1;
        week_dates[i].day = date_tm.tm_mday;
        if (week_day_labels[i]) {
            char buf[24];
            snprintf(buf, sizeof(buf), "%s\n%d", DOW_NAMES[date_tm.tm_wday], week_dates[i].day);
            lv_label_set_text(week_day_labels[i], buf);
        }
    }
    update_week_selection();
}

static int calendar_button_id_for_date(int y, int m, int d)
{
    if (d < 1 || d > 31) return -1;

    struct tm first = {};
    first.tm_year = y - 1900;
    first.tm_mon = m - 1;
    first.tm_mday = 1;
    if (mktime(&first) == (time_t)-1) return -1;

    return 7 + first.tm_wday + d - 1;
}

static void sync_calendar_selection(void)
{
    if (!calendar || sel_year == 0) return;

    lv_obj_t *cal_btnm = lv_calendar_get_btnmatrix(calendar);
    if (!cal_btnm) return;

    lv_buttonmatrix_clear_button_ctrl_all(cal_btnm, LV_BUTTONMATRIX_CTRL_CHECKED);

    const lv_calendar_date_t *shown = lv_calendar_get_showed_date(calendar);
    if (!shown || shown->year != sel_year || shown->month != sel_month) return;

    int btn_id = calendar_button_id_for_date(sel_year, sel_month, sel_day);
    if (btn_id < 7 || btn_id >= 49) return;
    if (lv_buttonmatrix_has_button_ctrl(cal_btnm, (uint32_t)btn_id, LV_BUTTONMATRIX_CTRL_DISABLED)) return;

    lv_buttonmatrix_set_selected_button(cal_btnm, (uint32_t)btn_id);
    lv_buttonmatrix_set_button_ctrl(cal_btnm, (uint32_t)btn_id, LV_BUTTONMATRIX_CTRL_CHECKED);
}

static void create_page2(lv_obj_t *tile);
static void create_page3(lv_obj_t *tile);
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h);

static void nav_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !tileview) return;

    lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(e);
    if (target) lv_tileview_set_tile(tileview, target, LV_ANIM_ON);
}

static void create_bottom_nav(lv_obj_t *page, int active_index)
{
    static const char *names[] = {"День", "Самочувствие", "Дом"};
    lv_obj_t *nav = lv_obj_create(page);
    lv_obj_remove_style_all(nav);
    lv_obj_set_size(nav, 1024, 60);
    lv_obj_set_pos(nav, 0, 540);
    lv_obj_set_style_bg_color(nav, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(nav, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    lv_obj_set_style_border_color(nav, COLOR_ACCENT, 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_btn_create(nav);
        lv_obj_set_size(btn, 280, 44);
        lv_obj_set_pos(btn, 18 + i * 300, 8);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        bool selected = i == active_index;
        lv_obj_set_style_bg_color(btn, selected ? COLOR_ACCENT : COLOR_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_color(label, selected ? COLOR_HIGHLIGHT : COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(label, &font_montserrat_16_cyr, 0);
        lv_label_set_text(label, names[i]);
        lv_obj_center(label);
        lv_obj_add_event_cb(btn, nav_click_cb, LV_EVENT_CLICKED, nav_tiles[i]);
    }

    lv_obj_t *version = lv_label_create(nav);
    lv_obj_set_width(version, 84);
    lv_obj_set_style_text_color(version, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(version, &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text_fmt(version, "v%s", FW_VERSION);
    lv_obj_set_pos(version, 928, 21);
}

static void reader_close_cb(lv_event_t *e)
{
    (void)e;
    if (reader_overlay) {
        lv_obj_delete(reader_overlay);
        reader_overlay = NULL;
    }
}

static void open_reader(const char *section, const char *title, const char *text, const char *meta)
{
    if (!title || !title[0]) return;
    if (reader_overlay) lv_obj_delete(reader_overlay);

    reader_overlay = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(reader_overlay);
    lv_obj_set_size(reader_overlay, 1024, 600);
    lv_obj_set_pos(reader_overlay, 0, 0);
    lv_obj_set_style_bg_color(reader_overlay, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(reader_overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(reader_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(reader_overlay);
    lv_obj_set_style_text_color(heading, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(heading, &font_montserrat_16_cyr, 0);
    lv_label_set_text(heading, section);
    lv_obj_set_pos(heading, 28, 22);

    lv_obj_t *close = lv_btn_create(reader_overlay);
    lv_obj_set_size(close, 120, 44);
    lv_obj_set_pos(close, 876, 16);
    lv_obj_set_style_bg_color(close, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(close, 10, 0);
    lv_obj_set_style_shadow_width(close, 0, 0);
    lv_obj_add_event_cb(close, reader_close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_label = lv_label_create(close);
    lv_obj_set_style_text_color(close_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(close_label, &font_montserrat_16_cyr, 0);
    lv_label_set_text(close_label, "Назад");
    lv_obj_center(close_label);

    lv_obj_t *body = lv_obj_create(reader_overlay);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, 968, 478);
    lv_obj_set_pos(body, 28, 86);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(body, 18, 0);
    lv_obj_set_style_bg_color(body, COLOR_TEXT_DIM, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(body, 4, LV_PART_SCROLLBAR);

    lv_obj_t *title_label = lv_label_create(body);
    lv_obj_set_width(title_label, 925);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title_label, &font_montserrat_24_cyr, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 0, 0);
    lv_obj_update_layout(title_label);

    int y = lv_obj_get_height(title_label) + 18;
    if (text && text[0]) {
        lv_obj_t *text_label = lv_label_create(body);
        lv_obj_set_width(text_label, 925);
        lv_obj_set_style_text_color(text_label, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(text_label, &font_montserrat_16_cyr, 0);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(text_label, text);
        lv_obj_set_pos(text_label, 0, y);
        lv_obj_update_layout(text_label);
        y += lv_obj_get_height(text_label) + 18;
    }

    if (meta && meta[0]) {
        lv_obj_t *meta_label = lv_label_create(body);
        lv_obj_set_width(meta_label, 925);
        lv_obj_set_style_text_color(meta_label, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(meta_label, &font_montserrat_16_cyr, 0);
        lv_label_set_long_mode(meta_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(meta_label, meta);
        lv_obj_set_pos(meta_label, 0, y);
    }
}

static void content_click_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (id >= 100) {
        int index = id - 100;
        if (index < MAX_NEWS_LINES) {
            const char *summary = news_summaries[index][0] ? news_summaries[index] : "Суммаризация недоступна.";
            open_reader("Новости", news_titles[index], summary, news_categories[index]);
        }
    } else if (id >= 0 && id < MAX_TASK_LINES) {
        char meta[40] = {};
        if (task_due[id][0]) snprintf(meta, sizeof(meta), "Срок: %s", task_due[id]);
        open_reader("Задачи", task_titles[id], NULL, meta);
    }
}

static lv_obj_t *day_label(lv_obj_t *parent, int x, int y, int w, const char *text,
                           const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(label, text);
    return label;
}

static void create_day_planner(lv_obj_t *page)
{
    // This is intentionally an overlay: the original dashboard stays below it
    // while the same update callbacks now target these glanceable components.
    lv_obj_t *surface = lv_obj_create(page);
    lv_obj_remove_style_all(surface);
    lv_obj_set_size(surface, 1024, 540);
    lv_obj_set_pos(surface, 0, 0);
    lv_obj_set_style_bg_color(surface, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_clear_flag(surface, LV_OBJ_FLAG_SCROLLABLE);

    lbl_datetime = day_label(surface, 28, 18, 600, "План на день", &font_montserrat_24_cyr, COLOR_TEXT);
    day_label(surface, 28, 54, 240, "НЕДЕЛЯ", &font_montserrat_16_cyr, COLOR_TEXT_DIM);

    int y, m, d;
    get_today_date(&y, &m, &d);
    if (sel_year == 0) {
        sel_year = y;
        sel_month = m;
        sel_day = d;
    }
    struct tm week_tm = {};
    week_tm.tm_year = y - 1900;
    week_tm.tm_mon = m - 1;
    week_tm.tm_mday = d;
    mktime(&week_tm);
    for (int i = 0; i < 7; i++) {
        struct tm date_tm = week_tm;
        date_tm.tm_mday += i;
        mktime(&date_tm);
        week_dates[i].year = date_tm.tm_year + 1900;
        week_dates[i].month = date_tm.tm_mon + 1;
        week_dates[i].day = date_tm.tm_mday;
        lv_obj_t *day = lv_obj_create(surface);
        lv_obj_remove_style_all(day);
        lv_obj_set_size(day, 78, 58);
        lv_obj_set_pos(day, 28 + i * 86, 78);
        lv_obj_set_style_radius(day, 10, 0);
        lv_obj_set_style_bg_color(day, i == 0 ? COLOR_ACCENT : COLOR_CARD, 0);
        lv_obj_set_style_bg_opa(day, LV_OPA_COVER, 0);
        lv_obj_add_flag(day, LV_OBJ_FLAG_CLICKABLE);
        char buf[24];
        snprintf(buf, sizeof(buf), "%s\n%d", DOW_NAMES[date_tm.tm_wday], week_dates[i].day);
        lv_obj_t *label = day_label(day, 0, 7, 78, buf, &font_montserrat_16_cyr,
                                    i == 0 ? COLOR_HIGHLIGHT : COLOR_TEXT_DIM);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        week_day_buttons[i] = day;
        week_day_labels[i] = label;
        if (i == 0) {
            lv_obj_t *dot = lv_obj_create(day);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 6, 6);
            lv_obj_set_pos(dot, 36, 46);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, COLOR_HIGHLIGHT, 0);
        }
        week_day_buttons[i] = day;
        lv_obj_add_event_cb(day, week_day_click_cb, LV_EVENT_CLICKED, &week_dates[i]);
    }

    lv_obj_t *plan = make_card(surface, 28, 154, 622, 360);
    lbl_sched_title = day_label(plan, 18, 15, 300, "Сегодня", &font_montserrat_24_cyr, COLOR_TEXT);
    hero_event_card = make_card(plan, 16, 56, 590, 105);
    lv_obj_set_style_bg_color(hero_event_card, COLOR_PANEL, 0);
    lbl_hero_status = day_label(hero_event_card, 14, 10, 540, "Следующее", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_hero_title = day_label(hero_event_card, 14, 35, 540, "Нет событий", &font_montserrat_24_cyr, COLOR_TEXT);
    lbl_hero_time = day_label(hero_event_card, 14, 72, 540, "", &font_montserrat_16_cyr, COLOR_HIGHLIGHT);
    events_scroll = lv_obj_create(plan);
    lv_obj_remove_style_all(events_scroll);
    lv_obj_set_size(events_scroll, 590, 160);
    lv_obj_set_pos(events_scroll, 16, 177);
    lv_obj_set_flex_flow(events_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(events_scroll, 10, 0);
    lv_obj_set_scrollbar_mode(events_scroll, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < MAX_EVENT_LINES; i++) {
        lbl_events[i] = day_label(events_scroll, 0, 0, 580, "", &font_montserrat_16_cyr, COLOR_TEXT);
        if (i >= 3) lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
    }
    lbl_no_events = day_label(events_scroll, 0, 0, 580, "Нет событий", &font_montserrat_16_cyr, COLOR_TEXT_DIM);

    lv_obj_t *right = make_card(surface, 670, 154, 326, 360);
    lbl_topbar_temp = day_label(right, 18, 15, 290, WEATHER_CITY, &font_montserrat_24_cyr, COLOR_TEXT);
    lbl_weather_detail = day_label(right, 18, 52, 290, "Нет данных о погоде", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    day_label(right, 18, 104, 290, "ТРАНСПОРТ", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_transport_out = lv_spangroup_create(right);
    lv_obj_set_pos(lbl_transport_out, 18, 132);
    lv_obj_set_width(lbl_transport_out, 290);
    lv_obj_set_style_text_font(lbl_transport_out, &font_montserrat_16_cyr, 0);
    lbl_transport_in = lv_spangroup_create(right);
    lv_obj_set_pos(lbl_transport_in, 18, 164);
    lv_obj_set_width(lbl_transport_in, 290);
    lv_obj_set_style_text_font(lbl_transport_in, &font_montserrat_16_cyr, 0);
    day_label(right, 18, 226, 290, "ВОЗДУХ ДОМА", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_air_primary = day_label(right, 18, 254, 290, "Нет данных о CO₂", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_air_detail = day_label(right, 18, 284, 290, "Нет данных о PM2.5", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    update_week_selection();
}

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
    lv_obj_set_scrollbar_mode(tileview, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tile1 = lv_tileview_add_tile(tileview, 0, 0, (lv_dir_t)LV_DIR_RIGHT);
    lv_obj_t *tile2 = lv_tileview_add_tile(tileview, 1, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t *tile3 = lv_tileview_add_tile(tileview, 2, 0, (lv_dir_t)LV_DIR_LEFT);
    nav_tiles[0] = tile1;
    nav_tiles[1] = tile2;
    nav_tiles[2] = tile3;

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
    lv_obj_set_width(lbl_datetime, 570);
    lv_label_set_long_mode(lbl_datetime, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_datetime, "...");
    lv_obj_set_pos(lbl_datetime, 15, 15);

    lbl_topbar_temp = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_topbar_temp, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_topbar_temp, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_topbar_temp, 190);
    lv_label_set_long_mode(lbl_topbar_temp, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_topbar_temp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_topbar_temp, WEATHER_CITY);
    lv_obj_set_pos(lbl_topbar_temp, 620, 20);

    lbl_weather_detail = lv_label_create(top_bar);
    lv_obj_set_style_text_color(lbl_weather_detail, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_weather_detail, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_weather_detail, 195);
    lv_label_set_long_mode(lbl_weather_detail, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(lbl_weather_detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(lbl_weather_detail, "");
    lv_obj_set_pos(lbl_weather_detail, 815, 20);

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
        static lv_draw_buf_t weather_draw_buf;
        lv_draw_buf_init(&weather_draw_buf, ICON_SIZE, ICON_SIZE, LV_COLOR_FORMAT_NATIVE,
                         0, canvas_buf, ICON_SIZE * ICON_SIZE * sizeof(lv_color_t));
        lv_canvas_set_draw_buf(weather_canvas, &weather_draw_buf);
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

    lv_obj_t *cal_btnm = lv_calendar_get_btnmatrix(calendar);
    lv_obj_set_style_bg_opa(cal_btnm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cal_btnm, 0, 0);
    lv_obj_set_style_pad_all(cal_btnm, 4, 0);
    lv_obj_set_style_bg_color(cal_btnm, lv_color_hex(0x24324A), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(cal_btnm, LV_OPA_40, LV_PART_ITEMS);
    lv_obj_set_style_border_width(cal_btnm, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(cal_btnm, lv_color_hex(0x56657A), LV_PART_ITEMS);
    lv_obj_set_style_radius(cal_btnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_text_color(cal_btnm, lv_color_hex(0xC9D2E3), LV_PART_ITEMS);
    const lv_style_selector_t cal_checked =
        (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_CHECKED;
    const lv_style_selector_t cal_pressed =
        (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_PRESSED;
    const lv_style_selector_t cal_disabled =
        (lv_style_selector_t)LV_PART_ITEMS | (lv_style_selector_t)LV_STATE_DISABLED;
    lv_obj_set_style_bg_color(cal_btnm, COLOR_HIGHLIGHT, cal_checked);
    lv_obj_set_style_text_color(cal_btnm, COLOR_TEXT, cal_checked);
    lv_obj_set_style_bg_opa(cal_btnm, LV_OPA_COVER, cal_checked);
    lv_obj_set_style_border_color(cal_btnm, lv_color_hex(0xBFE9FF), cal_checked);
    lv_obj_set_style_border_width(cal_btnm, 2, cal_checked);
    lv_obj_set_style_bg_color(cal_btnm, COLOR_HIGHLIGHT, cal_pressed);
    lv_obj_set_style_text_color(cal_btnm, COLOR_TEXT_DIM, cal_disabled);
    lv_obj_set_style_bg_opa(cal_btnm, LV_OPA_20, cal_disabled);

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

    // Hero event: the active or next calendar item is the primary glance target.
    hero_event_card = lv_obj_create(right_panel);
    lv_obj_remove_style_all(hero_event_card);
    lv_obj_set_size(hero_event_card, 559, 130);
    lv_obj_set_pos(hero_event_card, 20, 44);
    lv_obj_set_style_bg_color(hero_event_card, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(hero_event_card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hero_event_card, 8, 0);
    lv_obj_set_style_pad_all(hero_event_card, 12, 0);
    lv_obj_clear_flag(hero_event_card, LV_OBJ_FLAG_SCROLLABLE);

    lbl_hero_status = lv_label_create(hero_event_card);
    lv_obj_set_style_text_color(lbl_hero_status, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_hero_status, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_hero_status, 535);
    lv_label_set_text(lbl_hero_status, "Следующее");
    lv_obj_set_pos(lbl_hero_status, 12, 10);

    lbl_hero_title = lv_label_create(hero_event_card);
    lv_obj_set_style_text_color(lbl_hero_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_hero_title, &font_montserrat_24_cyr, 0);
    lv_obj_set_width(lbl_hero_title, 535);
    lv_label_set_long_mode(lbl_hero_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_hero_title, "Нет событий");
    lv_obj_set_pos(lbl_hero_title, 12, 38);

    lbl_hero_time = lv_label_create(hero_event_card);
    lv_obj_set_style_text_color(lbl_hero_time, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_hero_time, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_hero_time, 535);
    lv_label_set_long_mode(lbl_hero_time, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(lbl_hero_time, "");
    lv_obj_set_pos(lbl_hero_time, 12, 88);

    // Compact event area for the remaining events.
    events_scroll = lv_obj_create(right_panel);
    lv_obj_remove_style_all(events_scroll);
    lv_obj_set_size(events_scroll, 579, 225);
    lv_obj_set_pos(events_scroll, 0, 185);
    lv_obj_set_style_bg_opa(events_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(events_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(events_scroll, 4, 0);
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

    lv_obj_t *tr_sep = lv_obj_create(right_panel);
    lv_obj_remove_style_all(tr_sep);
    lv_obj_set_size(tr_sep, 559, 1);
    lv_obj_set_pos(tr_sep, 20, 428);
    lv_obj_set_style_bg_color(tr_sep, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(tr_sep, LV_OPA_60, 0);
    lv_obj_clear_flag(tr_sep, LV_OBJ_FLAG_SCROLLABLE);

    lbl_transport_out = lv_spangroup_create(right_panel);
    lv_obj_set_style_text_color(lbl_transport_out, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_transport_out, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_transport_out, 540);
    lv_spangroup_set_overflow(lbl_transport_out, LV_SPAN_OVERFLOW_ELLIPSIS);
    lv_obj_set_pos(lbl_transport_out, 20, 438);

    lbl_transport_in = lv_spangroup_create(right_panel);
    lv_obj_set_style_text_color(lbl_transport_in, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_transport_in, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(lbl_transport_in, 540);
    lv_spangroup_set_overflow(lbl_transport_in, LV_SPAN_OVERFLOW_ELLIPSIS);
    lv_obj_set_pos(lbl_transport_in, 20, 462);

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

    create_day_planner(tile1);
    create_bottom_nav(tile1, 0);
    create_bottom_nav(tile2, 1);
    create_bottom_nav(tile3, 2);

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

    if (lbl_topbar_temp) {
        char buf[80];
        snprintf(buf, sizeof(buf), "%s %s %.0fC",
                 WEATHER_CITY,
                 weather_code_to_text(data->weather_code),
                 data->temp);
        lv_label_set_text(lbl_topbar_temp, buf);
    }

    if (lbl_weather_detail) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%.0f%% %.0fкм/ч %.0f/%.0fC",
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
        if (lbl_hero_status) lv_label_set_text(lbl_hero_status, "Календарь");
        if (lbl_hero_title) lv_label_set_text(lbl_hero_title, "Нет данных");
        char error[64] = {};
        if (lbl_hero_time && bridge_copy_last_error(error, sizeof(error))) lv_label_set_text(lbl_hero_time, error);
        if (lbl_no_events) lv_obj_add_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < MAX_EVENT_LINES; i++) {
            if (lbl_events[i]) lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (now_line) lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    // Update the schedule title to reflect fetched date
    if (data->year > 0) {
        sel_year  = data->year;
        sel_month = data->month;
        sel_day   = data->day;
        update_schedule_title(sel_year, sel_month, sel_day);
        update_week_selection();
    }

    // The day view deliberately stays glanceable: hero plus three following rows.
    int display_count = data->count < 4 ? data->count : 4;
    bool selected_today = is_today(sel_year, sel_month, sel_day);

    if (lbl_no_events) lv_obj_add_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
    if (now_line) lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);

    int now_min = 0;
    if (selected_today) {
        time_t now;
        time(&now);
        struct tm tinfo;
        localtime_r(&now, &tinfo);
        now_min = tinfo.tm_hour * 60 + tinfo.tm_min;
    }

    int hero_idx = -1;
    bool hero_active = false;
    bool has_all_day = false;

    if (display_count > 0) {
        if (selected_today) {
            for (int i = 0; i < display_count; i++) {
                const bridge_cal_event_t *ev = &data->events[i];
                if (ev->all_day) {
                    has_all_day = true;
                    continue;
                }
                int start_min = ev->start_hour * 60 + ev->start_min;
                int end_min = ev->end_hour * 60 + ev->end_min;
                if (now_min >= start_min && now_min < end_min) {
                    hero_idx = i;
                    hero_active = true;
                    break;
                }
            }
            if (hero_idx < 0) {
                for (int i = 0; i < display_count; i++) {
                    const bridge_cal_event_t *ev = &data->events[i];
                    if (ev->all_day) continue;
                    int start_min = ev->start_hour * 60 + ev->start_min;
                    if (now_min < start_min) {
                        hero_idx = i;
                        break;
                    }
                }
            }
        } else {
            for (int i = 0; i < display_count; i++) {
                const bridge_cal_event_t *ev = &data->events[i];
                if (ev->all_day) {
                    has_all_day = true;
                    continue;
                }
                hero_idx = i;
                break;
            }
        }
    }

    if (hero_idx >= 0) {
        const bridge_cal_event_t *ev = &data->events[hero_idx];
        char time_buf[96];
        if (ev->all_day) {
            snprintf(time_buf, sizeof(time_buf), "Весь день");
        } else {
            int start_min = ev->start_hour * 60 + ev->start_min;
            int end_min = ev->end_hour * 60 + ev->end_min;
            if (selected_today && hero_active) {
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d-%02d:%02d  до %02d:%02d",
                         ev->start_hour, ev->start_min,
                         ev->end_hour, ev->end_min,
                         ev->end_hour, ev->end_min);
            } else if (selected_today && start_min > now_min) {
                int diff = start_min - now_min;
                if (diff < 60) {
                    snprintf(time_buf, sizeof(time_buf), "%02d:%02d-%02d:%02d  через %d мин",
                             ev->start_hour, ev->start_min,
                             ev->end_hour, ev->end_min,
                             diff);
                } else {
                    snprintf(time_buf, sizeof(time_buf), "%02d:%02d  через %dч %02dм",
                             ev->start_hour, ev->start_min,
                             diff / 60, diff % 60);
                }
            } else if (selected_today && end_min <= now_min) {
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d-%02d:%02d  день почти свободен",
                         ev->start_hour, ev->start_min,
                         ev->end_hour, ev->end_min);
            } else {
                snprintf(time_buf, sizeof(time_buf), "%02d:%02d-%02d:%02d",
                         ev->start_hour, ev->start_min,
                         ev->end_hour, ev->end_min);
            }
        }

        if (lbl_hero_status) {
            lv_label_set_text(lbl_hero_status,
                ev->all_day ? "Весь день" :
                (hero_active ? "Сейчас" : (selected_today ? "Следующее" : "Выбранный день")));
        }
        if (lbl_hero_title) {
            lv_label_set_text(lbl_hero_title, ev->summary);
            lv_obj_set_style_text_color(lbl_hero_title, COLOR_TEXT, 0);
        }
        if (lbl_hero_time) {
            lv_label_set_text(lbl_hero_time, time_buf);
            lv_obj_set_style_text_color(lbl_hero_time, cal_color(ev->cal_idx), 0);
        }
        if (hero_event_card) {
            lv_obj_set_style_border_width(hero_event_card, 1, 0);
            lv_obj_set_style_border_color(hero_event_card, COLOR_ACCENT, 0);
        }
    } else {
        if (lbl_hero_status) lv_label_set_text(lbl_hero_status, selected_today ? "Сегодня" : "Выбранный день");
        if (lbl_hero_title) {
            lv_label_set_text(lbl_hero_title,
                has_all_day ? (selected_today ? "На сегодня больше событий нет" : "На эту дату больше событий нет") : "Свободный день");
            lv_obj_set_style_text_color(lbl_hero_title, COLOR_TEXT, 0);
        }
        if (lbl_hero_time) {
            lv_label_set_text(lbl_hero_time,
                has_all_day ? "События на весь день ниже" :
                (selected_today ? "Нет событий в расписании" : "На эту дату нет событий"));
            lv_obj_set_style_text_color(lbl_hero_time, COLOR_TEXT_DIM, 0);
        }
        if (hero_event_card) {
            lv_obj_set_style_border_width(hero_event_card, 0, 0);
        }
    }

    int list_idx = 0;
    for (int i = 0; i < display_count && list_idx < MAX_EVENT_LINES; i++) {
        if (i == hero_idx) continue;
        if (!lbl_events[list_idx]) continue;

        const bridge_cal_event_t *ev = &data->events[i];
        char buf[96];
        if (ev->all_day) {
            snprintf(buf, sizeof(buf), "Весь день  %s", ev->summary);
        } else {
            snprintf(buf, sizeof(buf), "%02d:%02d  %s", ev->start_hour, ev->start_min, ev->summary);
        }

        lv_label_set_text(lbl_events[list_idx], buf);
        lv_obj_set_style_text_color(lbl_events[list_idx],
            ev->all_day ? COLOR_TEXT_DIM : cal_color(ev->cal_idx), 0);
        lv_obj_set_style_bg_opa(lbl_events[list_idx], LV_OPA_TRANSP, 0);
        lv_obj_clear_flag(lbl_events[list_idx], LV_OBJ_FLAG_HIDDEN);
        list_idx++;
    }

    for (int i = list_idx; i < MAX_EVENT_LINES; i++) {
        if (lbl_events[i]) {
            lv_label_set_text(lbl_events[i], "");
            lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (list_idx == 0 && lbl_no_events) {
        lv_label_set_text(lbl_no_events, hero_idx >= 0 ? "Больше событий нет" : "");
        if (hero_idx >= 0) lv_obj_clear_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_color_t time_color(int mins)
{
    if (mins <= 2)  return COLOR_GOOD;
    if (mins <= 6)  return COLOR_HIGHLIGHT;
    if (mins <= 12) return COLOR_TEXT;
    return COLOR_TEXT_DIM;
}

static void append_transport_stop(lv_obj_t *spangroup, const char *label, const bridge_transport_stop_t *stop)
{
    while (lv_spangroup_get_span_count(spangroup) > 0) {
        lv_spangroup_delete_span(spangroup, lv_spangroup_get_child(spangroup, 0));
    }

    lv_span_t *title = lv_spangroup_new_span(spangroup);
    lv_span_set_text(title, label);
    lv_style_set_text_color(lv_span_get_style(title), COLOR_TEXT_DIM);

    if (stop->count == 0) {
        lv_span_t *s = lv_spangroup_new_span(spangroup);
        lv_span_set_text(s, "нет данных");
        lv_style_set_text_color(lv_span_get_style(s), COLOR_TEXT_DIM);
        return;
    }

    int n = stop->count < 3 ? stop->count : 3;
    for (int i = 0; i < n; i++) {
        const bridge_transport_vehicle_t *v = &stop->vehicles[i];
        int mins = (v->seconds_left + 30) / 60;
        if (mins < 1) mins = 1;

        // Separator
        if (i > 0) {
            lv_span_t *sep = lv_spangroup_new_span(spangroup);
            lv_span_set_text(sep, " | ");
            lv_style_set_text_color(lv_span_get_style(sep), COLOR_TEXT_DIM);
        }

        lv_span_t *route = lv_spangroup_new_span(spangroup);
        lv_span_set_text(route, v->line_number);
        lv_style_set_text_color(lv_span_get_style(route), COLOR_HIGHLIGHT);

        lv_span_t *colon = lv_spangroup_new_span(spangroup);
        lv_span_set_text(colon, ":");
        lv_style_set_text_color(lv_span_get_style(colon), COLOR_TEXT_DIM);

        char time_buf[16];
        snprintf(time_buf, sizeof(time_buf), "%dм", mins);
        lv_span_t *time_span = lv_spangroup_new_span(spangroup);
        lv_span_set_text(time_span, time_buf);
        lv_style_set_text_color(lv_span_get_style(time_span), time_color(mins));
    }

    if (stop->count > n) {
        char more_buf[12];
        snprintf(more_buf, sizeof(more_buf), " +%d", stop->count - n);
        lv_span_t *more = lv_spangroup_new_span(spangroup);
        lv_span_set_text(more, more_buf);
        lv_style_set_text_color(lv_span_get_style(more), COLOR_TEXT_DIM);
    }

    lv_spangroup_refr_mode(spangroup);
}

void ui_dashboard_update_transport(const bridge_transport_t *data)
{
    if (!data || !data->valid) return;

    if (lbl_transport_out) {
        append_transport_stop(lbl_transport_out, "Из центра  ", &data->stops[0]);
    }
    if (lbl_transport_in) {
        append_transport_stop(lbl_transport_in, "В центр    ", &data->stops[1]);
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
            calendar_highlighted_dates[0].year = y;
            calendar_highlighted_dates[0].month = m;
            calendar_highlighted_dates[0].day = d;
            lv_calendar_set_highlighted_dates(calendar, calendar_highlighted_dates, 1);
            lv_calendar_set_showed_date(calendar, y, m);
            last_cal_year = y;
            last_cal_mon = m;
            last_cal_day = d;
            refresh_week_strip();
            if (selected_non_today_at_us == 0) {
                return_calendar_to_today();
            } else {
                sync_calendar_selection();
            }
        }
    }

    // Update bottom bar
    if (lbl_bottom) {
        char buf[128];
        char error[64] = {};
        bridge_copy_last_error(error, sizeof(error));
        snprintf(buf, sizeof(buf), "WiFi:%s H:%.0f/%.0fKB Br:%s v%s",
                 wifi_is_connected() ? "OK" : "--",
                 esp_get_free_heap_size() / 1024.0f,
                 esp_get_free_internal_heap_size() / 1024.0f,
                 error,
                 FW_VERSION);
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
    lv_obj_set_style_arc_color(arc_readiness, COLOR_ACCENT, LV_PART_MAIN);
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
    lv_obj_t *tasks_panel = make_card(tile, 5, 285, 505, 245);

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
        lv_label_set_long_mode(lbl_task_lines[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl_task_lines[i], "");
        lv_obj_set_pos(lbl_task_lines[i], 28, y);
        lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lbl_task_lines[i], content_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);
        lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_no_tasks = lv_label_create(tasks_panel);
    lv_obj_set_style_text_color(lbl_no_tasks, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_no_tasks, &font_montserrat_16_cyr, 0);
    lv_label_set_text(lbl_no_tasks, "Нет задач");
    lv_obj_set_pos(lbl_no_tasks, 15, 38);

    // ===== BOTTOM RIGHT: News (505x305) =====
    lv_obj_t *news_panel = make_card(tile, 514, 285, 505, 245);

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
        lv_label_set_long_mode(lbl_news_lines[i], LV_LABEL_LONG_DOT);
        lv_label_set_text(lbl_news_lines[i], "");
        lv_obj_set_pos(lbl_news_lines[i], 30, y);
        lv_obj_add_flag(lbl_news_lines[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(lbl_news_lines[i], content_click_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)(100 + i));
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
        if (n > MAX_TASK_LINES) n = MAX_TASK_LINES;
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
                strncpy(task_titles[i], data->tasks[i].title, sizeof(task_titles[i]) - 1);
                task_titles[i][sizeof(task_titles[i]) - 1] = '\0';
                strncpy(task_due[i], data->tasks[i].due, sizeof(task_due[i]) - 1);
                task_due[i][sizeof(task_due[i]) - 1] = '\0';
            } else {
                if (task_prio_bars[i]) lv_obj_add_flag(task_prio_bars[i], LV_OBJ_FLAG_HIDDEN);
                if (lbl_task_lines[i]) lv_obj_add_flag(lbl_task_lines[i], LV_OBJ_FLAG_HIDDEN);
                task_titles[i][0] = '\0';
                task_due[i][0] = '\0';
            }
        }
    }

    // News — with dots and age
    if (data->news_valid) {
        int n = data->news_count;
        if (n > MAX_NEWS_LINES) n = MAX_NEWS_LINES;
        if (n == 0) {
            if (lbl_no_news) lv_obj_clear_flag(lbl_no_news, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (lbl_no_news) lv_obj_add_flag(lbl_no_news, LV_OBJ_FLAG_HIDDEN);
        }

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
                    lv_label_set_text(lbl_news_lines[i], data->news[i].title);
                    lv_obj_clear_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
                }
                if (lbl_news_age[i]) {
                    lv_label_set_text(lbl_news_age[i], data->news[i].category);
                    lv_obj_clear_flag(lbl_news_age[i], LV_OBJ_FLAG_HIDDEN);
                }
                strncpy(news_titles[i], data->news[i].title, sizeof(news_titles[i]) - 1);
                news_titles[i][sizeof(news_titles[i]) - 1] = '\0';
                strncpy(news_summaries[i], data->news[i].summary, sizeof(news_summaries[i]) - 1);
                news_summaries[i][sizeof(news_summaries[i]) - 1] = '\0';
                strncpy(news_categories[i], data->news[i].category, sizeof(news_categories[i]) - 1);
                news_categories[i][sizeof(news_categories[i]) - 1] = '\0';
            } else {
                if (news_dots[i]) lv_obj_add_flag(news_dots[i], LV_OBJ_FLAG_HIDDEN);
                if (lbl_news_lines[i]) lv_obj_add_flag(lbl_news_lines[i], LV_OBJ_FLAG_HIDDEN);
                if (lbl_news_age[i]) lv_obj_add_flag(lbl_news_age[i], LV_OBJ_FLAG_HIDDEN);
                news_titles[i][0] = '\0';
                news_summaries[i][0] = '\0';
                news_categories[i][0] = '\0';
            }
        }
    }
}

// ===== PAGE 3: HA Control — rooms =====

#define COLOR_LIGHT_ON  lv_color_hex(0xE7C96B)
#define COLOR_LIGHT_OFF lv_color_hex(0xDCE4DE)

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

static void ota_btn_cb(lv_event_t *e)
{
    (void)e;
    request_ota_check();
}

static void create_page3(lv_obj_t *tile)
{
    // Title
    lv_obj_t *pg_title = lv_label_create(tile);
    lv_obj_set_style_text_color(pg_title, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(pg_title, &font_montserrat_24_cyr, 0);
    lv_label_set_text(pg_title, "Дом");
    lv_obj_set_pos(pg_title, 15, 8);

    // OTA update button
    lv_obj_t *btn_ota = lv_btn_create(tile);
    lv_obj_set_size(btn_ota, 120, 30);
    lv_obj_set_pos(btn_ota, 890, 6);
    lv_obj_set_style_bg_color(btn_ota, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(btn_ota, 15, 0);
    lv_obj_set_style_shadow_width(btn_ota, 0, 0);
    lv_obj_add_event_cb(btn_ota, ota_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ota_lbl = lv_label_create(btn_ota);
    lv_obj_set_style_text_color(ota_lbl, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ota_lbl, &font_montserrat_16_cyr, 0);
    lv_label_set_text(ota_lbl, "Обновить");
    lv_obj_center(ota_lbl);

    // 2x2 grid of room cards
    int pw = 500, ph = 235;
    int positions[MAX_ROOMS][2] = {
        {5, 40}, {512, 40}, {5, 280}, {512, 280}
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

    if (lbl_air_primary && lbl_air_detail) {
        const bridge_sensor_t *co2 = NULL;
        const bridge_sensor_t *pm25 = NULL;
        if (data->sensors_valid) {
            for (int i = 0; i < data->sensor_count; i++) {
                const bridge_sensor_t *sensor = &data->sensors[i];
                if (!co2 && strstr(sensor->unit, "ppm")) co2 = sensor;
                if (!pm25 && (strstr(sensor->name, "PM2.5") || strstr(sensor->name, "PM25") ||
                              strstr(sensor->name, "pm2.5") || strstr(sensor->name, "pm25"))) {
                    pm25 = sensor;
                }
            }
        }

        if (co2) {
            char text[48];
            snprintf(text, sizeof(text), "CO₂: %s %s", co2->value, co2->unit);
            int value = atoi(co2->value);
            lv_label_set_text(lbl_air_primary, text);
            lv_obj_set_style_text_color(lbl_air_primary,
                value < 800 ? COLOR_GOOD : value < 1000 ? COLOR_WARN : COLOR_BAD, 0);
        } else {
            lv_label_set_text(lbl_air_primary, "Нет данных о CO₂");
            lv_obj_set_style_text_color(lbl_air_primary, COLOR_TEXT_DIM, 0);
        }

        if (pm25) {
            char text[48];
            snprintf(text, sizeof(text), "PM2.5: %s %s", pm25->value, pm25->unit);
            float value = strtof(pm25->value, NULL);
            lv_label_set_text(lbl_air_detail, text);
            lv_obj_set_style_text_color(lbl_air_detail,
                value <= 12.0f ? COLOR_GOOD : value <= 35.4f ? COLOR_WARN : COLOR_BAD, 0);
        } else {
            lv_label_set_text(lbl_air_detail, "Нет данных о PM2.5");
            lv_obj_set_style_text_color(lbl_air_detail, COLOR_TEXT_DIM, 0);
        }
    }

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
                        lv_obj_set_style_text_color(room_sensor_val_lbl[r][s], COLOR_TEXT, 0);

                        // A ppm unit is the only unambiguous CO2 signal in this contract.
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
