#include "brightness_runtime.h"
#include "ui_dashboard.h"
#include "photo_frame.h"
#include "config.h"
#include "wifi_manager.h"
#include "bridge.h"
#include "action_feedback_runtime.h"
#include "web_sync.h"
#include "weather_icons.h"
#include "ui_strings.h"
#include "lvgl.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include <cctype>
#include <cstring>

// Declared in main.cpp
extern void request_calendar_date(int year, int month, int day);
extern void request_light_toggle(const char *entity_id);
extern void request_climate_action(const char *entity_id, const char *action, float temperature, const char *mode);
extern void request_ota_check(void);
extern void request_recovery_ap(void);
extern void request_stop_recovery_ap(void);
extern void wavesahre_rgb_lcd_bl_on(void);
extern void wavesahre_rgb_lcd_bl_off(void);

// Custom font with Cyrillic support (for HA calendar events)
LV_FONT_DECLARE(font_montserrat_16_cyr);
LV_FONT_DECLARE(font_montserrat_24_cyr);

// --- UI element references ---
static lv_obj_t *lbl_datetime    = NULL;
static lv_obj_t *lbl_topbar_temp = NULL;
static lv_obj_t *lbl_bottom      = NULL;
static lv_obj_t *lbl_connection_status = NULL;
static lv_obj_t *calendar        = NULL;
static lv_obj_t *calendar_header = NULL;

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
static lv_obj_t *lbl_day_context = NULL;
static lv_obj_t *lbl_day_summary = NULL;
#define INITIAL_EVENT_LINES 16
static std::vector<lv_obj_t *> lbl_events(INITIAL_EVENT_LINES, nullptr);
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
static constexpr int PAGE_COUNT = 4;
static constexpr int SETTINGS_PAGE = 3;
static lv_obj_t *nav_tiles[PAGE_COUNT] = {};
static lv_obj_t *week_day_buttons[7] = {};
static lv_obj_t *week_day_labels[7] = {};
static lv_calendar_date_t week_dates[7] = {};
static lv_obj_t *reader_overlay = NULL;
static lv_obj_t *reader_body = NULL;
static lv_obj_t *reader_text_label = NULL;
static lv_obj_t *reader_meta_label = NULL;
static const char *reader_text_fallback_key = NULL;
static const char *reader_meta_format_key = NULL;
static char reader_meta_value[64] = {};
static lv_obj_t *settings_web_link = NULL;
static lv_obj_t *settings_qr = NULL;
static lv_obj_t *settings_ap_name = NULL;
static lv_obj_t *settings_ap_url = NULL;
static lv_obj_t *settings_ap_help = NULL;
static lv_obj_t *settings_password = NULL;
static lv_obj_t *settings_reveal_btn = NULL;
static lv_obj_t *settings_reveal_label = NULL;
static lv_obj_t *settings_ap_button_label = NULL;
static lv_obj_t *settings_device_status = NULL;
static lv_obj_t *settings_reference_status = NULL, *settings_reference_label = NULL;
static lv_obj_t *settings_reference_button = NULL, *settings_reference_panel = NULL;
static lv_obj_t *settings_comparison_label = NULL, *settings_result_label = NULL;
static lv_obj_t *settings_step_buttons[2] = {}, *settings_mark_buttons[2] = {};
static lv_obj_t *settings_step_labels[2] = {}, *settings_mark_labels[2] = {};
static lv_obj_t *settings_step_size_label = NULL, *settings_target_label = NULL;
static uint8_t settings_step_size = 5;
static bool settings_dark_target = false;
static brightness_comparison_t settings_rendered_comparison = {};
struct calibration_action_t { uint64_t id; bool historical, dark; uint8_t step; };
static calibration_action_t calibration_actions[4] = {};
static void refresh_reference_test();
static char settings_qr_url[192] = {};
static int64_t settings_reveal_until_us = 0;
static void refresh_settings(void);
static void settings_privacy_tick(lv_timer_t *);
static void home_feedback_tick(lv_timer_t *);
static bool settings_is_active(void);
static lv_obj_t *page1_schedule_panel = NULL;
static lv_obj_t *page1_overview_panel = NULL;
static lv_obj_t *day_weather_card = NULL, *day_transport_card = NULL, *day_air_card = NULL;
static lv_obj_t *lbl_weather_condition = NULL, *lbl_weather_freshness = NULL, *lbl_day_city = NULL;
static lv_obj_t *lbl_transport_freshness = NULL, *lbl_calendar_freshness = NULL;
static lv_obj_t *page2_health_panel = NULL;
static lv_obj_t *page2_tasks_panel = NULL;
static lv_obj_t *page2_news_panel = NULL;
static lv_obj_t *page3_root = NULL;
static web_preferences_t ui_preferences = {};
static bool ui_has_preferences = false;
static bool ui_screen_off = false;
static int64_t wake_override_until = 0;
static int64_t ui_last_touch = 0;
static bool wake_swallow_until_release = false;

// bridge_data_t includes bounded Evening News summaries and is larger than the
// LVGL task stack. All LVGL timers run serially, so one static snapshot keeps
// the copy out of that stack without concurrent access.
static bridge_data_t ui_bridge_snapshot = {};
static bridge_cal_data_t ui_calendar_snapshot = {};
static char ui_recovery_ssid[64] = {};
static char ui_recovery_password[64] = {};
static char ui_recovery_url[192] = {};
static bool ui_recovery_active = false;

// Page 2: Health + Tasks + News (redesigned)
// Readiness arc
static lv_obj_t *arc_readiness = NULL;
static lv_obj_t *lbl_readiness_val = NULL;
static lv_obj_t *lbl_readiness_label = NULL;

// Health summary cells: value + label + trend, without nested cards.
#define NUM_METRIC_CARDS 6
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
static lv_obj_t *room_light_feedback[MAX_ROOMS][MAX_ROOM_LIGHTS] = {};
static bool room_light_on[MAX_ROOMS][MAX_ROOM_LIGHTS] = {};
static char room_light_entity_ids[MAX_ROOMS][MAX_ROOM_LIGHTS][48] = {};
// Sensor cards per room (value + name labels)
static lv_obj_t *room_sensor_cards[MAX_ROOMS][MAX_ROOM_SENSORS] = {};
static lv_obj_t *room_sensor_val_lbl[MAX_ROOMS][MAX_ROOM_SENSORS] = {};
static lv_obj_t *room_sensor_name_lbl[MAX_ROOMS][MAX_ROOM_SENSORS] = {};
static lv_obj_t *home_air_values[4] = {};
static lv_obj_t *home_air_names[4] = {};
static lv_obj_t *favorite_buttons[4] = {};
static lv_obj_t *favorite_labels[4] = {};
static lv_obj_t *favorite_feedback[4] = {};
static bool favorite_available[4] = {}, favorite_on[4] = {};
static lv_obj_t *home_air_card = NULL, *home_climate_card = NULL;
static lv_obj_t *home_room_cards[MAX_ROOMS] = {};
static lv_obj_t *home_climate_name = NULL;
static lv_obj_t *home_climate_current = NULL;
static lv_obj_t *home_climate_target = NULL;
static lv_obj_t *home_climate_power = NULL;
static lv_obj_t *home_climate_feedback = NULL;
static lv_obj_t *home_climate_selected = NULL;
static lv_obj_t *home_climate_temp_buttons[2] = {};
static bool home_temp_proposed = false;
static uint64_t home_temp_due_ms = 0;
static float home_temp_confirmed = 22.0f;
static lv_obj_t *home_climate_mode_btns[4] = {};
static float home_climate_target_value = 22.0f;
static bool home_climate_on = false;
static bool home_climate_known = false;
static char home_climate_entity[48] = {};

#define COLOR_NOW lv_color_hex(0xC96A5A)

// Track last calendar date to avoid unnecessary updates
static int last_cal_year = 0;
static int last_cal_mon  = 0;
static int last_cal_day  = 0;
static int last_cal_event_count = -2;

// Currently selected date for schedule display
static int sel_year  = 0;
static int sel_month = 0;
static int sel_day   = 0;
static int64_t selected_non_today_at_us = 0;
static lv_calendar_date_t calendar_highlighted_dates[1] = {};

#define CALENDAR_AUTO_RETURN_US (5LL * 60LL * 1000000LL)

// Track last weather code to avoid unnecessary redraws
static int last_weather_code = -1;

static const char *weekday_name(int tm_wday) { return ui_day_name((tm_wday + 6) % 7); }
static void refresh_calendar_header(lv_obj_t *header);

#define MAX_STATIC_UI_LABELS 128
struct static_ui_label_t { lv_obj_t *label; const char *key; };
static static_ui_label_t static_ui_labels[MAX_STATIC_UI_LABELS] = {};

static void static_ui_label_delete_cb(lv_event_t *event)
{
    lv_obj_t *label = (lv_obj_t *)lv_event_get_target(event);
    for (auto &binding : static_ui_labels) {
        if (binding.label == label) {
            binding = {};
            break;
        }
    }
}

static void bind_static_ui_label(lv_obj_t *label, const char *russian_key)
{
    if (!label || !russian_key) return;
    static_ui_label_t *slot = nullptr;
    for (auto &binding : static_ui_labels) {
        if (binding.label == label) { slot = &binding; break; }
        if (!binding.label && !slot) slot = &binding;
    }
    if (!slot) return;
    const bool is_new = slot->label == nullptr;
    slot->label = label;
    slot->key = russian_key;
    if (is_new) lv_obj_add_event_cb(label, static_ui_label_delete_cb, LV_EVENT_DELETE, nullptr);
}

static void unbind_static_ui_label(lv_obj_t *label)
{
    for (auto &binding : static_ui_labels) {
        if (binding.label == label) {
            binding = {};
            return;
        }
    }
}

static void set_static_ui_label(lv_obj_t *label, const char *russian_key)
{
    if (!label) return;
    bind_static_ui_label(label, russian_key);
    lv_label_set_text(label, ui_text(russian_key));
}

static void refresh_static_ui_labels(void)
{
    for (const auto &binding : static_ui_labels) {
        if (binding.label && binding.key) lv_label_set_text(binding.label, ui_text(binding.key));
    }
}

static void set_calendar_day_names(void)
{
    static const char *days_ru[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    static const char *days_en[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    static const char *days_sr[] = {"Po", "Ut", "Sr", "Če", "Pe", "Su", "Ne"};
    if (!calendar) return;
    const char **names = !strcmp(ui_strings_locale(), "en") ? days_en :
                         !strcmp(ui_strings_locale(), "sr-Latn") ? days_sr : days_ru;
    lv_calendar_set_day_names(calendar, names);
    refresh_calendar_header(calendar_header);
}

static void refresh_calendar_header(lv_obj_t *header)
{
    if (!header) return;
    lv_obj_t *calendar_obj = lv_obj_get_parent(header);
    const lv_calendar_date_t *date = lv_calendar_get_showed_date(calendar_obj);
    lv_obj_t *label = lv_obj_get_child(header, 1);
    if (!date || !label || date->month < 1 || date->month > 12) return;
    char text[48];
    snprintf(text, sizeof(text), "%d %s", date->year, ui_month_name_full(date->month - 1));
    lv_obj_set_style_text_font(label, &font_montserrat_16_cyr, 0);
    lv_label_set_text(label, text);
}

static void calendar_header_locale_cb(lv_event_t *event)
{
    refresh_calendar_header((lv_obj_t *)lv_event_get_current_target(event));
}

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
static void update_day_context(int y, int m, int d, int event_count);
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

static bool clock_is_night_for(const web_preferences_t &prefs, const struct tm &t) {
    if (!prefs.night_enabled) return false;
    int now_min = t.tm_hour * 60 + t.tm_min;
    int start = (prefs.night_start[0] - '0') * 600 +
                (prefs.night_start[1] - '0') * 60 +
                (prefs.night_start[3] - '0') * 10 +
                prefs.night_start[4] - '0';
    int end = (prefs.night_end[0] - '0') * 600 +
              (prefs.night_end[1] - '0') * 60 +
              (prefs.night_end[3] - '0') * 10 +
              prefs.night_end[4] - '0';
    return start == end || (start < end ? now_min >= start && now_min < end
                                         : now_min >= start || now_min < end);
}

static bool clock_is_night(const struct tm &t) {
    return ui_has_preferences && clock_is_night_for(ui_preferences, t);
}
bool ui_dashboard_apply_brightness(const web_preferences_t *prefs) {
    if (!prefs || !prefs->backlight_percent) return true;
    if (!brightness_runtime_ready()) return false;
    time_t now; time(&now); struct tm t = {}; localtime_r(&now, &t);
    bool night = prefs->night_dim && clock_is_night_for(*prefs, t);
    bool wake = esp_timer_get_time() / 1000000 < wake_override_until;
    uint8_t night_level = prefs->night_dim ? prefs->night_backlight_percent : prefs->backlight_percent;
    return brightness_runtime_schedule(prefs->backlight_percent, night_level, night, wake);
}

static void set_screen_off(bool off) {
    if (off) { ui_dashboard_cancel_brightness_test(); ui_dashboard_hide_sensitive(); }
    if (ui_screen_off == off) return;
    ui_screen_off = off;
    if (off) wavesahre_rgb_lcd_bl_off();
    else wavesahre_rgb_lcd_bl_on();
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

void ui_dashboard_refresh_ha(void)
{
    if (bridge_copy_data(&ui_bridge_snapshot)) ui_dashboard_update_ha(&ui_bridge_snapshot);
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
    if (ui_calendar_snapshot.year != y || ui_calendar_snapshot.month != m || ui_calendar_snapshot.day != d) {
        bridge_cal_data_t waiting = {};
        waiting.year = y; waiting.month = m; waiting.day = d;
        ui_dashboard_update_ha_calendar(&waiting);
    }
    if (is_today(y, m, d)) {
        set_static_ui_label(lbl_sched_title, "Расписание");
        if (btn_today) lv_obj_add_flag(btn_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        unbind_static_ui_label(lbl_sched_title);
        char buf[48];
        const char *mon = (m >= 1 && m <= 12) ? ui_month_name(m - 1) : "???";
        snprintf(buf, sizeof(buf), "%s: %s %d", ui_text("Расписание"), mon, d);
        lv_label_set_text(lbl_sched_title, buf);
        if (btn_today) lv_obj_clear_flag(btn_today, LV_OBJ_FLAG_HIDDEN);
    }
}

static void update_day_context(int y, int m, int d, int event_count)
{
    if (!lbl_day_context || !lbl_day_summary || m < 1 || m > 12) return;

    if (event_count < 0) {
        lv_label_set_text(lbl_day_context, ui_text("Расписание"));
        lv_label_set_text(lbl_day_summary, event_count == -2 ? ui_text("Нет данных календаря") : ui_text("Загружаю расписание"));
        return;
    }

    if (event_count == 0) {
        lv_label_set_text(lbl_day_context, ui_text("Свободный день"));
    } else {
        char summary[40];
        snprintf(summary, sizeof(summary), "%d %s", event_count,
                 event_count == 1 ? ui_text("событие") : (event_count < 5 ? ui_text("события") : ui_text("событий")));
        lv_label_set_text(lbl_day_context, summary);
    }
    lv_label_set_text(lbl_day_summary, is_today(y, m, d) ? ui_text("Сегодня") : ui_text("Выбранный день"));
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
            snprintf(buf, sizeof(buf), "%s\n%d", weekday_name(date_tm.tm_wday), week_dates[i].day);
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
static void create_settings_page(lv_obj_t *tile);
static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h);

static void nav_click_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !tileview) return;

    lv_obj_t *target = (lv_obj_t *)lv_event_get_user_data(e);
    if (target) {
        ui_dashboard_cancel_brightness_test();
        ui_dashboard_hide_sensitive();
        lv_tileview_set_tile(tileview, target, LV_ANIM_ON);
    }
}

static void create_bottom_nav(lv_obj_t *page, int active_index)
{
    static const char *names[] = {"День", "Сводка", "Дом", "Настройки"};
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
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *btn = lv_btn_create(nav);
        lv_obj_set_size(btn, 216, 44);
        lv_obj_set_pos(btn, 16 + i * 228, 8);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        bool selected = i == active_index;
        lv_obj_set_style_bg_color(btn, selected ? COLOR_ACCENT : COLOR_BG, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_color(label, selected ? COLOR_HIGHLIGHT : COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(label, &font_montserrat_16_cyr, 0);
        lv_obj_set_width(label, 200);
        set_static_ui_label(label, names[i]);
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
        photo_frame_reader_closed();
    }
}

static void refresh_reader_locale(void)
{
    if (!reader_overlay) return;
    const int32_t scroll_y = reader_body ? lv_obj_get_scroll_y(reader_body) : 0;
    if (reader_text_label && reader_text_fallback_key)
        lv_label_set_text(reader_text_label, ui_text(reader_text_fallback_key));
    if (reader_meta_label && reader_meta_format_key) {
        char meta[96];
        snprintf(meta, sizeof(meta), ui_text(reader_meta_format_key), reader_meta_value);
        lv_label_set_text(reader_meta_label, meta);
    }
    if (reader_body) {
        lv_obj_update_layout(reader_body);
        lv_obj_scroll_to_y(reader_body, scroll_y, LV_ANIM_OFF);
    }
}

static void open_reader(const char *section_key, const char *title, const char *text,
                        const char *meta_format_key, const char *meta_value,
                        const char *text_fallback_key = nullptr)
{
    if (!title || !title[0]) return;
    ui_dashboard_hide_sensitive();
    if (reader_overlay) lv_obj_delete(reader_overlay);
    reader_body = reader_text_label = reader_meta_label = nullptr;
    reader_text_fallback_key = text_fallback_key;
    reader_meta_format_key = meta_format_key;
    snprintf(reader_meta_value, sizeof(reader_meta_value), "%s", meta_value ? meta_value : "");

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
    set_static_ui_label(heading, section_key);
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
    set_static_ui_label(close_label, "Назад");
    lv_obj_center(close_label);

    reader_body = lv_obj_create(reader_overlay);
    lv_obj_remove_style_all(reader_body);
    lv_obj_set_size(reader_body, 968, 478);
    lv_obj_set_pos(reader_body, 28, 86);
    lv_obj_set_scroll_dir(reader_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(reader_body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_right(reader_body, 18, 0);
    lv_obj_set_style_bg_color(reader_body, COLOR_TEXT_DIM, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(reader_body, 4, LV_PART_SCROLLBAR);

    lv_obj_t *title_label = lv_label_create(reader_body);
    lv_obj_set_width(title_label, 925);
    lv_obj_set_style_text_color(title_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title_label, &font_montserrat_24_cyr, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 0, 0);
    lv_obj_update_layout(title_label);

    int y = lv_obj_get_height(title_label) + 18;
    const char *visible_text = text_fallback_key ? ui_text(text_fallback_key) : text;
    if (visible_text && visible_text[0]) {
        reader_text_label = lv_label_create(reader_body);
        lv_obj_set_width(reader_text_label, 925);
        lv_obj_set_style_text_color(reader_text_label, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(reader_text_label, &font_montserrat_16_cyr, 0);
        lv_label_set_long_mode(reader_text_label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(reader_text_label, visible_text);
        lv_obj_set_pos(reader_text_label, 0, y);
        lv_obj_update_layout(reader_text_label);
        y += lv_obj_get_height(reader_text_label) + 18;
    }

    if (meta_format_key && meta_value && meta_value[0]) {
        reader_meta_label = lv_label_create(reader_body);
        lv_obj_set_width(reader_meta_label, 925);
        lv_obj_set_style_text_color(reader_meta_label, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(reader_meta_label, &font_montserrat_16_cyr, 0);
        lv_label_set_long_mode(reader_meta_label, LV_LABEL_LONG_WRAP);
        char meta[96];
        snprintf(meta, sizeof(meta), ui_text(meta_format_key), meta_value);
        lv_label_set_text(reader_meta_label, meta);
        lv_obj_set_pos(reader_meta_label, 0, y);
    }
}

static void content_click_cb(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    if (id >= 100) {
        int index = id - 100;
        if (index < MAX_NEWS_LINES) {
            const char *summary = news_summaries[index][0] ? news_summaries[index] : nullptr;
            open_reader("Новости", news_titles[index], summary, nullptr, nullptr,
                        summary ? nullptr : "Суммаризация недоступна.");
        }
    } else if (id >= 0 && id < MAX_TASK_LINES) {
        open_reader("Задачи", task_titles[id], nullptr,
                    task_due[id][0] ? "Срок: %s" : nullptr,
                    task_due[id][0] ? task_due[id] : nullptr);
    }
}

#if defined(SIMULATOR)
void ui_dashboard_sim_open_reader(void)
{
    static char long_text[1800];
    if (!long_text[0]) {
        for (size_t i = 0; i + 2 < sizeof(long_text); i += 2) {
            long_text[i] = 'x';
            long_text[i + 1] = ' ';
        }
        long_text[sizeof(long_text) - 1] = '\0';
    }
    open_reader("Задачи", "Today", long_text, "Срок: %s", "2030-01-01");
    if (reader_body) lv_obj_scroll_to_y(reader_body, 80, LV_ANIM_OFF);
}

void ui_dashboard_sim_close_reader(void)
{
    if (reader_overlay) lv_obj_delete(reader_overlay);
    reader_overlay = reader_body = reader_text_label = reader_meta_label = nullptr;
    reader_text_fallback_key = reader_meta_format_key = nullptr;
}

int32_t ui_dashboard_sim_reader_scroll_y(void)
{
    return reader_body ? lv_obj_get_scroll_y(reader_body) : -1;
}
#endif

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

static lv_obj_t *day_static_label(lv_obj_t *parent, int x, int y, int w, const char *key,
                                  const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = day_label(parent, x, y, w, ui_text(key), font, color);
    set_static_ui_label(label, key);
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

    lbl_datetime = day_static_label(surface, 28, 18, 600, "План на день", &font_montserrat_24_cyr, COLOR_TEXT);
    day_static_label(surface, 28, 54, 240, "НЕДЕЛЯ", &font_montserrat_16_cyr, COLOR_TEXT_DIM);

    day_static_label(surface, 670, 20, 326, "Погода и транспорт", &font_montserrat_16_cyr, COLOR_TEXT_DIM);

    int y, m, d;
    get_today_date(&y, &m, &d);
    if (sel_year == 0) {
        sel_year = y;
        sel_month = m;
        sel_day = d;
    }
    update_day_context(sel_year, sel_month, sel_day, -1);
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
        snprintf(buf, sizeof(buf), "%s\n%d", weekday_name(date_tm.tm_wday), week_dates[i].day);
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

    lbl_connection_status = day_static_label(surface, 28, 519, 968, "Подключение…",
                                      &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lv_label_set_long_mode(lbl_connection_status, LV_LABEL_LONG_DOT);
    lv_obj_set_height(lbl_connection_status, 19);

    lv_obj_t *plan = make_card(surface, 28, 154, 622, 360);
    page1_schedule_panel = plan;
    lbl_sched_title = day_static_label(plan, 18, 15, 290, "Сегодня", &font_montserrat_24_cyr, COLOR_TEXT);
    lbl_day_context = day_label(plan, 330, 18, 260, "", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_day_summary = day_label(plan, 330, 38, 260, "", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_calendar_freshness = day_label(plan, 18, 332, 580, "", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    hero_event_card = make_card(plan, 16, 66, 590, 146);
    lv_obj_set_style_bg_color(hero_event_card, COLOR_PANEL, 0);
    lbl_hero_status = day_static_label(hero_event_card, 14, 10, 540, "Следующее", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_hero_title = day_static_label(hero_event_card, 14, 35, 540, "Нет событий", &font_montserrat_24_cyr, COLOR_TEXT);
    lv_label_set_long_mode(lbl_hero_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_height(lbl_hero_title, 60);
    lbl_hero_time = day_label(hero_event_card, 14, 108, 540, "", &font_montserrat_16_cyr, COLOR_HIGHLIGHT);
    events_scroll = lv_obj_create(plan);
    lv_obj_remove_style_all(events_scroll);
    lv_obj_set_size(events_scroll, 590, 102);
    lv_obj_set_pos(events_scroll, 16, 224);
    lv_obj_set_flex_flow(events_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(events_scroll, 10, 0);
    lv_obj_set_scroll_dir(events_scroll, LV_DIR_VER);
    lv_obj_remove_flag(events_scroll, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_set_scrollbar_mode(events_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_color(events_scroll, COLOR_TEXT_DIM, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(events_scroll, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(events_scroll, 4, LV_PART_SCROLLBAR);
    for (int i = 0; i < INITIAL_EVENT_LINES; i++) {
        lbl_events[i] = day_label(events_scroll, 0, 0, 580, "", &font_montserrat_16_cyr, COLOR_TEXT);
        if (i >= 3) lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
    }
    lbl_no_events = day_static_label(events_scroll, 0, 0, 580, "Нет событий", &font_montserrat_16_cyr, COLOR_TEXT_DIM);

    lv_obj_t *right = lv_obj_create(surface);
    lv_obj_remove_style_all(right);
    lv_obj_set_pos(right, 670, 66);
    lv_obj_set_size(right, 326, 448);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    page1_overview_panel = right;
    day_weather_card = make_card(right, 0, 0, 326, 196);
    lbl_day_city = day_label(day_weather_card, 18, 12, 290, ui_has_preferences ? ui_preferences.city : WEATHER_CITY,
              &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    // Reuse the existing 48px canvas and buffer rather than allocating another image.
    if (weather_canvas && canvas_buf) {
        lv_obj_set_parent(weather_canvas, day_weather_card);
        lv_obj_set_pos(weather_canvas, 18, 48);
        lv_obj_clear_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    lbl_topbar_temp = day_label(day_weather_card, 92, 36, 215, "--", &lv_font_montserrat_48, COLOR_TEXT);
    lbl_weather_condition = day_static_label(day_weather_card, 18, 99, 290, "Нет данных о погоде", &font_montserrat_16_cyr, COLOR_TEXT);
    lbl_weather_detail = day_label(day_weather_card, 18, 124, 290, "", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_weather_freshness = day_label(day_weather_card, 18, 151, 290, "", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lv_label_set_long_mode(lbl_weather_freshness, LV_LABEL_LONG_WRAP);
    day_transport_card = make_card(right, 0, 208, 326, 136);
    day_static_label(day_transport_card, 18, 10, 290, "ТРАНСПОРТ", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_transport_out = lv_spangroup_create(day_transport_card);
    lv_obj_set_pos(lbl_transport_out, 18, 36);
    lv_obj_set_width(lbl_transport_out, 290);
    lv_obj_set_style_text_font(lbl_transport_out, &font_montserrat_16_cyr, 0);
    lv_spangroup_set_mode(lbl_transport_out, LV_SPAN_MODE_BREAK);
    lv_spangroup_set_max_lines(lbl_transport_out, 1);
    lv_spangroup_set_overflow(lbl_transport_out, LV_SPAN_OVERFLOW_ELLIPSIS);
    lbl_transport_in = lv_spangroup_create(day_transport_card);
    lv_obj_set_pos(lbl_transport_in, 18, 62);
    lv_obj_set_width(lbl_transport_in, 290);
    lv_obj_set_style_text_font(lbl_transport_in, &font_montserrat_16_cyr, 0);
    lv_spangroup_set_mode(lbl_transport_in, LV_SPAN_MODE_BREAK);
    lv_spangroup_set_max_lines(lbl_transport_in, 1);
    lv_spangroup_set_overflow(lbl_transport_in, LV_SPAN_OVERFLOW_ELLIPSIS);
    lbl_transport_freshness = day_label(day_transport_card, 18, 90, 290, "", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lv_label_set_long_mode(lbl_transport_freshness, LV_LABEL_LONG_WRAP);
    day_air_card = make_card(right, 0, 356, 326, 92);
    day_static_label(day_air_card, 18, 8, 290, "ВОЗДУХ ДОМА", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_air_primary = day_static_label(day_air_card, 18, 34, 290, "Гостиная: нет данных CO2", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    lbl_air_detail = day_static_label(day_air_card, 18, 60, 290, "Кабинет: нет данных CO2", &font_montserrat_16_cyr, COLOR_TEXT_DIM);
    update_week_selection();
}

void ui_dashboard_create(void)
{
    ui_last_touch = esp_timer_get_time() / 1000000;
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
    lv_obj_t *tile3 = lv_tileview_add_tile(tileview, 2, 0, (lv_dir_t)(LV_DIR_LEFT | LV_DIR_RIGHT));
    lv_obj_t *tile4 = lv_tileview_add_tile(tileview, 3, 0, (lv_dir_t)LV_DIR_LEFT);
    lv_obj_add_event_cb(tileview, [](lv_event_t *) { ui_dashboard_cancel_brightness_test(); ui_dashboard_hide_sensitive(); }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(tileview, [](lv_event_t *) { ui_dashboard_cancel_brightness_test(); ui_dashboard_hide_sensitive(); }, LV_EVENT_SCROLL_BEGIN, nullptr);
    nav_tiles[0] = tile1;
    nav_tiles[1] = tile2;
    nav_tiles[2] = tile3;
    nav_tiles[3] = tile4;

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
    set_calendar_day_names();

    calendar_header = lv_calendar_header_arrow_create(calendar);
    lv_obj_set_style_text_color(calendar_header, COLOR_TEXT, 0);
    lv_obj_add_event_cb(calendar_header, calendar_header_locale_cb, LV_EVENT_VALUE_CHANGED, nullptr);
    refresh_calendar_header(calendar_header);

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
    set_static_ui_label(lbl_sched_title, "Расписание");
    lv_obj_set_pos(lbl_sched_title, 20, 10);

    // ui_text("Сегодня") button
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
    set_static_ui_label(btn_lbl, "Сегодня");
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
    set_static_ui_label(lbl_hero_status, "Следующее");
    lv_obj_set_pos(lbl_hero_status, 12, 10);

    lbl_hero_title = lv_label_create(hero_event_card);
    lv_obj_set_style_text_color(lbl_hero_title, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_hero_title, &font_montserrat_24_cyr, 0);
    lv_obj_set_width(lbl_hero_title, 535);
    lv_label_set_long_mode(lbl_hero_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    set_static_ui_label(lbl_hero_title, "Нет событий");
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

    for (int i = 0; i < INITIAL_EVENT_LINES; i++) {
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
    set_static_ui_label(lbl_no_events, "Нет событий");

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
    set_static_ui_label(lbl_bottom, "WiFi: ---");
    lv_obj_set_pos(lbl_bottom, 10, 2);

    // ===== PAGE 2: Health + Tasks + News =====
    create_page2(tile2);

    // ===== PAGE 3: HA Control =====
    create_page3(tile3);
    create_settings_page(tile4);

    create_day_planner(tile1);
    create_bottom_nav(tile1, 0);
    create_bottom_nav(tile2, 1);
    create_bottom_nav(tile3, 2);
    create_bottom_nav(tile4, 3);

    // Timers
    lv_timer_create(timer_time_cb, 5000, NULL);
    lv_timer_create(settings_privacy_tick, 250, NULL);
    lv_timer_create(home_feedback_tick, 100, NULL);
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
    const int language = !strcmp(ui_strings_locale(), "en") ? 1 :
                         !strcmp(ui_strings_locale(), "sr-Latn") ? 2 : 0;
    static const char *labels[3][12] = {
        {"Ясно", "Облачно", "Туман", "Морось", "Ледяная морось", "Дождь", "Ледяной дождь", "Снег", "Снежная крупа", "Ливни", "Снегопад", "Гроза"},
        {"Clear", "Cloudy", "Fog", "Drizzle", "Freezing drizzle", "Rain", "Freezing rain", "Snow", "Snow grains", "Showers", "Snow showers", "Storm"},
        {"Vedro", "Oblačno", "Magla", "Rosulja", "Ledena rosulja", "Kiša", "Ledena kiša", "Sneg", "Susnežica", "Pljuskovi", "Snežni pljuskovi", "Oluja"},
    };
    int index = -1;
    if (code == 0) index = 0;
    else if (code >= 1 && code <= 3) index = 1;
    else if (code == 45 || code == 48) index = 2;
    else if (code >= 51 && code <= 55) index = 3;
    else if (code >= 56 && code <= 57) index = 4;
    else if (code >= 61 && code <= 65) index = 5;
    else if (code >= 66 && code <= 67) index = 6;
    else if (code >= 71 && code <= 75) index = 7;
    else if (code == 77) index = 8;
    else if (code >= 80 && code <= 82) index = 9;
    else if (code >= 85 && code <= 86) index = 10;
    else if (code >= 95 && code <= 99) index = 11;
    return index >= 0 ? labels[language][index] : "???";
}

static void update_receipt_label(lv_obj_t *label, bool valid, bool failed, int64_t received_ms) {
    if (!label) return;
    char buf[128];
    if (!valid) snprintf(buf, sizeof(buf), "%s", ui_text(failed ? "Не удалось обновить" : "Ещё не обновлялось"));
    else {
        int64_t age = esp_timer_get_time() / 1000 - received_ms;
        if (age < 0) age = 0;
        if (failed) {
            snprintf(buf, sizeof(buf), ui_text("Последние данные: %d мин назад"), (int)(age / 60000));
            lv_label_set_text(label, buf);
            return;
        }
        if (age < 60000) snprintf(buf, sizeof(buf), "%s", ui_text("Получено дисплеем только что"));
        else snprintf(buf, sizeof(buf), ui_text("Получено дисплеем %d мин назад"), (int)(age / 60000));
    }
    lv_label_set_text(label, buf);
}

void ui_dashboard_update_weather(const bridge_weather_t *data)
{
    if (!data) return;
    if (data != &ui_bridge_snapshot.weather) ui_bridge_snapshot.weather = *data;
    if (lbl_day_city) lv_label_set_text(lbl_day_city, ui_has_preferences ? ui_preferences.city : WEATHER_CITY);
    update_receipt_label(lbl_weather_freshness, data->valid, data->last_attempt_failed, data->received_ms);
    if (!data->valid) {
        if (lbl_topbar_temp) lv_label_set_text(lbl_topbar_temp, "--");
        if (lbl_weather_condition) set_static_ui_label(lbl_weather_condition, "Нет данных о погоде");
        if (lbl_weather_detail) lv_label_set_text(lbl_weather_detail, "");
        if (weather_canvas) lv_obj_add_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (weather_canvas && canvas_buf) {
        if (!ui_has_preferences || ui_preferences.visible_weather) lv_obj_clear_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);
        if (data->weather_code != last_weather_code) {
            weather_icon_draw(weather_canvas, data->weather_code, COLOR_CARD);
            last_weather_code = data->weather_code;
        }
    }
    if (lbl_topbar_temp) {
        unbind_static_ui_label(lbl_topbar_temp);
        char buf[32]; snprintf(buf, sizeof(buf), "%.0f°", data->temp);
        lv_label_set_text(lbl_topbar_temp, buf);
    }
    if (lbl_weather_condition) {
        unbind_static_ui_label(lbl_weather_condition);
        lv_label_set_text(lbl_weather_condition, weather_code_to_text(data->weather_code));
    }
    if (lbl_weather_detail) {
        char buf[96] = {};
        if (data->daily_count > 0) snprintf(buf, sizeof(buf), ui_text("Сегодня: %.0f° / %.0f°"),
                            data->daily[0].temp_min, data->daily[0].temp_max);
        unbind_static_ui_label(lbl_weather_detail); lv_label_set_text(lbl_weather_detail, buf);
    }
}

void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data)
{
    if (data && data->year > 0 && (data->year != sel_year || data->month != sel_month || data->day != sel_day)) return;
    if (data) {
        update_receipt_label(lbl_calendar_freshness, data->valid, data->last_attempt_failed, data->received_ms);
        if (data->valid && data->sources_partial && !data->last_attempt_failed && lbl_calendar_freshness) {
            int64_t age = esp_timer_get_time() / 1000 - data->received_ms;
            if (age < 0) age = 0;
            char text[128]; snprintf(text, sizeof(text), ui_text("Часть календарей недоступна · %d мин назад"), (int)(age / 60000));
            lv_label_set_text(lbl_calendar_freshness, text);
        }
    }
    if (!data || !data->valid) {
        ui_calendar_snapshot.valid = false;
        last_cal_event_count = -2;
        update_day_context(sel_year, sel_month, sel_day, -2);
        if (lbl_hero_status) set_static_ui_label(lbl_hero_status, "Календарь");
        if (lbl_hero_title) set_static_ui_label(lbl_hero_title, data && !data->last_attempt_failed ? "Загружаю расписание" : "Нет данных календаря");
        if (lbl_hero_time) lv_label_set_text(lbl_hero_time, data && !data->last_attempt_failed ? "" : ui_text("Проверьте подключение в настройках"));
        if (lbl_no_events) lv_obj_add_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
        for (lv_obj_t *label : lbl_events) {
            if (label) lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        }
        if (now_line) lv_obj_add_flag(now_line, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    ui_calendar_snapshot = *data;

    // Update the schedule title to reflect fetched date
    last_cal_event_count = data->count;
    if (data->year > 0) {
        sel_year  = data->year;
        sel_month = data->month;
        sel_day   = data->day;
        update_schedule_title(sel_year, sel_month, sel_day);
        update_week_selection();
    }
    update_day_context(sel_year, sel_month, sel_day, data->count);
    if (lbl_day_context && data->sources_partial) lv_label_set_text(lbl_day_context, ui_text("Неполное расписание"));
    else if (lbl_day_context && !data->completeness_known && data->count == 0) lv_label_set_text(lbl_day_context, ui_text("Нет событий в ответе"));

    const int display_count = data->count;
    while (lbl_events.size() < static_cast<size_t>(display_count)) {
        lv_obj_t *label = day_label(events_scroll, 0, 0, 580, "", &font_montserrat_16_cyr, COLOR_TEXT);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        lbl_events.push_back(label);
    }
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
            snprintf(time_buf, sizeof(time_buf), "%s", ui_text("Весь день"));
        } else {
            int start_min = ev->start_hour * 60 + ev->start_min;
            int end_min = ev->end_hour * 60 + ev->end_min;
            if (selected_today && hero_active) {
                snprintf(time_buf, sizeof(time_buf), ui_text("%02d:%02d-%02d:%02d  до %02d:%02d"),
                         ev->start_hour, ev->start_min,
                         ev->end_hour, ev->end_min,
                         ev->end_hour, ev->end_min);
            } else if (selected_today && start_min > now_min) {
                int diff = start_min - now_min;
                if (diff < 60) {
                    snprintf(time_buf, sizeof(time_buf), ui_text("%02d:%02d-%02d:%02d  через %d мин"),
                             ev->start_hour, ev->start_min,
                             ev->end_hour, ev->end_min,
                             diff);
                } else {
                    snprintf(time_buf, sizeof(time_buf), ui_text("%02d:%02d  через %dч %02dм"),
                             ev->start_hour, ev->start_min,
                             diff / 60, diff % 60);
                }
            } else if (selected_today && end_min <= now_min) {
                snprintf(time_buf, sizeof(time_buf), ui_text("%02d:%02d-%02d:%02d  день почти свободен"),
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
                ev->all_day ? ui_text("Весь день") :
                (hero_active ? ui_text("Сейчас") : (selected_today ? ui_text("Следующее") : ui_text("Выбранный день"))));
        }
        if (lbl_hero_title) {
            unbind_static_ui_label(lbl_hero_title);
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
        if (lbl_hero_status) lv_label_set_text(lbl_hero_status, selected_today ? ui_text("Сегодня") : ui_text("Выбранный день"));
        if (lbl_hero_title) {
            const char *message_key = data->sources_partial ? "Список событий неполный" :
                (!data->completeness_known && display_count == 0) ? "Нет событий в ответе" : has_all_day ?
                (selected_today ? "На сегодня больше событий нет" : "На эту дату больше событий нет") : "Свободный день";
            set_static_ui_label(lbl_hero_title, message_key);
            lv_obj_set_style_text_color(lbl_hero_title, COLOR_TEXT, 0);
        }
        if (lbl_hero_time) {
            lv_label_set_text(lbl_hero_time,
                data->sources_partial ? ui_text("Часть календарей недоступна") :
                has_all_day ? ui_text("События на весь день ниже") :
                (selected_today ? ui_text("Нет событий в расписании") : ui_text("На эту дату нет событий")));
            lv_obj_set_style_text_color(lbl_hero_time, COLOR_TEXT_DIM, 0);
        }
        if (hero_event_card) {
            lv_obj_set_style_border_width(hero_event_card, 0, 0);
        }
    }

    int list_idx = 0;
    for (int i = 0; i < display_count; i++) {
        if (i == hero_idx) continue;
        if (!lbl_events[list_idx]) continue;

        const bridge_cal_event_t *ev = &data->events[i];
        char buf[96];
        if (ev->all_day) {
            snprintf(buf, sizeof(buf), ui_text("Весь день  %s"), ev->summary);
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

    for (size_t i = list_idx; i < lbl_events.size(); i++) {
        if (lbl_events[i]) {
            lv_label_set_text(lbl_events[i], "");
            lv_obj_add_flag(lbl_events[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (list_idx == 0 && lbl_no_events) {
        set_static_ui_label(lbl_no_events, data->sources_partial || !data->completeness_known ? "Нет событий в ответе" : hero_idx >= 0 ? "Больше событий нет" : "Нет событий");
        lv_obj_clear_flag(lbl_no_events, LV_OBJ_FLAG_HIDDEN);
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
        lv_span_set_text(s, ui_text("нет данных"));
        lv_style_set_text_color(lv_span_get_style(s), COLOR_TEXT_DIM);
        return;
    }

    int n = stop->count < 2 ? stop->count : 2;
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
        snprintf(time_buf, sizeof(time_buf), ui_text("%dм"), mins);
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
    if (!data) return;
    if (data != &ui_bridge_snapshot.transport) ui_bridge_snapshot.transport = *data;
    update_receipt_label(lbl_transport_freshness, data->valid, data->last_attempt_failed, data->received_ms);
    if (!data->valid) {
        bridge_transport_stop_t empty = {};
        if (lbl_transport_out) append_transport_stop(lbl_transport_out, ui_text("Из центра  "), &empty);
        if (lbl_transport_in) append_transport_stop(lbl_transport_in, ui_text("В центр    "), &empty);
        return;
    }

    if (lbl_transport_out) {
        append_transport_stop(lbl_transport_out, ui_text("Из центра  "), &data->stops[0]);
    }
    if (lbl_transport_in) {
        append_transport_stop(lbl_transport_in, ui_text("В центр    "), &data->stops[1]);
    }
}

void ui_dashboard_update_time(void)
{
    refresh_settings();
    time_t now;
    time(&now);
    struct tm tinfo;
    localtime_r(&now, &tinfo);

    // Update top bar clock
    if (lbl_datetime) {
        char buf[64];
        unbind_static_ui_label(lbl_datetime);
        if (ui_has_preferences && ui_preferences.clock_12h) {
            int hour = tinfo.tm_hour % 12;
            if (!hour) hour = 12;
            snprintf(buf, sizeof(buf), "%s, %d %s %d  %d:%02d %s",
                     weekday_name(tinfo.tm_wday), tinfo.tm_mday, ui_month_name(tinfo.tm_mon),
                     tinfo.tm_year + 1900, hour, tinfo.tm_min,
                     tinfo.tm_hour >= 12 ? "PM" : "AM");
        } else {
            snprintf(buf, sizeof(buf), "%s, %d %s %d  %02d:%02d",
                     weekday_name(tinfo.tm_wday), tinfo.tm_mday, ui_month_name(tinfo.tm_mon),
                     tinfo.tm_year + 1900, tinfo.tm_hour, tinfo.tm_min);
        }
        lv_label_set_text(lbl_datetime, buf);
    }

    if (ui_has_preferences) {
        int64_t now_seconds = esp_timer_get_time() / 1000000;
        bool idle_off = !ui_preferences.idle_photos &&
                        now_seconds - ui_last_touch >= ui_preferences.photo_idle_seconds;
        bool sleep = ((clock_is_night(tinfo) && !ui_preferences.night_dim) || idle_off) &&
                     now_seconds >= wake_override_until;
        set_screen_off(sleep);
        if (!sleep) ui_dashboard_apply_brightness(&ui_preferences);
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

    // The legacy footer is covered by navigation; keep failures visible here.
    if (lbl_connection_status) {
        char sync[96] = {}, data[64] = {}, status[192];
        web_sync_copy_diagnostic(sync, sizeof(sync));
        bridge_copy_last_error(data, sizeof(data));
        bool data_ok = !strcmp(data, "OK") || (!strcmp(data, "Fetching...") && bridge_has_data());
        bool healthy = wifi_is_connected() && !strcmp(sync, "Sync:OK") && data_ok;
        if (healthy) lv_obj_add_flag(lbl_connection_status, LV_OBJ_FLAG_HIDDEN);
        else {
            snprintf(status, sizeof(status), "%s", ui_text(wifi_is_connected() ? "Нет связи с Bridge. Проверьте настройки" : "Нет Wi-Fi. Проверьте настройки"));
            unbind_static_ui_label(lbl_connection_status);
            lv_label_set_text(lbl_connection_status, status);
            lv_obj_clear_flag(lbl_connection_status, LV_OBJ_FLAG_HIDDEN);
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
        unbind_static_ui_label(lbl_bottom);
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
    // ===== TOP: compact health summary =====
    lv_obj_t *health_banner = make_card(tile, 5, 5, 1014, 180);
    page2_health_panel = health_banner;

    // Title
    lv_obj_t *lbl_htitle = lv_label_create(health_banner);
    lv_obj_set_style_text_color(lbl_htitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_htitle, &font_montserrat_24_cyr, 0);
    set_static_ui_label(lbl_htitle, "Здоровье");
    lv_obj_set_pos(lbl_htitle, 15, 8);

    // Readiness arc (left side)
    arc_readiness = lv_arc_create(health_banner);
    lv_obj_set_size(arc_readiness, 112, 112);
    lv_obj_set_pos(arc_readiness, 20, 42);
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
    lv_label_set_text(lbl_readiness_val, "--");
    lv_obj_set_style_text_font(lbl_readiness_val, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(lbl_readiness_val, 54, 78);

    // Readiness label (below arc)
    lbl_readiness_label = lv_label_create(health_banner);
    lv_obj_set_style_text_color(lbl_readiness_label, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(lbl_readiness_label, &font_montserrat_16_cyr, 0);
    set_static_ui_label(lbl_readiness_label, "Готовность");
    lv_obj_set_pos(lbl_readiness_label, 33, 154);

    // A flat 3x2 grid keeps readiness dominant and removes "cards in cards".
    static const char *metric_names[] = {"Сон", "Шаги", "Пульс", "ЧСС покоя", "ВСР", "SpO2"};
    const int grid_x = 170, cell_w = 275;
    for (int i = 0; i < NUM_METRIC_CARDS; i++) {
        int row = i / 3;
        int col = i % 3;
        int x = grid_x + col * cell_w;
        int y = 42 + row * 66;
        metric_cards[i].lbl_name = lv_label_create(health_banner);
        lv_obj_set_style_text_color(metric_cards[i].lbl_name, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(metric_cards[i].lbl_name, &font_montserrat_16_cyr, 0);
        set_static_ui_label(metric_cards[i].lbl_name, metric_names[i]);
        lv_obj_set_pos(metric_cards[i].lbl_name, x + 10, y);

        metric_cards[i].lbl_value = lv_label_create(health_banner);
        lv_obj_set_style_text_color(metric_cards[i].lbl_value, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(metric_cards[i].lbl_value, &font_montserrat_24_cyr, 0);
        lv_label_set_text(metric_cards[i].lbl_value, "---");
        lv_obj_set_pos(metric_cards[i].lbl_value, x + 10, y + 18);

        metric_cards[i].lbl_trend = lv_label_create(health_banner);
        lv_obj_set_style_text_color(metric_cards[i].lbl_trend, COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(metric_cards[i].lbl_trend, &font_montserrat_16_cyr, 0);
        lv_label_set_text(metric_cards[i].lbl_trend, "");
        lv_obj_set_pos(metric_cards[i].lbl_trend, x + 120, y + 24);
    }
    for (int i = 1; i < 3; i++) {
        lv_obj_t *rule = lv_obj_create(health_banner);
        lv_obj_remove_style_all(rule);
        lv_obj_set_size(rule, 1, 118);
        lv_obj_set_pos(rule, grid_x + i * cell_w - 4, 42);
        lv_obj_set_style_bg_color(rule, COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    }

    // ===== BOTTOM LEFT: Tasks =====
    lv_obj_t *tasks_panel = make_card(tile, 5, 190, 505, 340);
    page2_tasks_panel = tasks_panel;

    lv_obj_t *lbl_ttitle = lv_label_create(tasks_panel);
    lv_obj_set_style_text_color(lbl_ttitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_ttitle, &font_montserrat_24_cyr, 0);
    set_static_ui_label(lbl_ttitle, "Задачи");
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
    set_static_ui_label(lbl_no_tasks, "Нет задач");
    lv_obj_set_pos(lbl_no_tasks, 15, 38);

    // ===== BOTTOM RIGHT: News =====
    lv_obj_t *news_panel = make_card(tile, 514, 190, 505, 340);
    page2_news_panel = news_panel;

    lv_obj_t *lbl_ntitle = lv_label_create(news_panel);
    lv_obj_set_style_text_color(lbl_ntitle, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(lbl_ntitle, &font_montserrat_24_cyr, 0);
    set_static_ui_label(lbl_ntitle, "Новости");
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
    set_static_ui_label(lbl_no_news, "Нет новостей");
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
    if (data != &ui_bridge_snapshot) ui_bridge_snapshot = *data;

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
                r >= 80 ? ui_text("Отлично") : r >= 50 ? ui_text("Норма") : ui_text("Низко"));
        }

        // Flat summary: 0=Sleep, 1=Steps, 2=HR, 3=RHR, 4=HRV, 5=SpO2.
        char val[16], trend[24];

        // Sleep
        snprintf(val, sizeof(val), "%.1fh", data->health.sleep);
        int s10 = (int)(data->health.sleep * 10), sp10 = (int)(data->health.sleep_prev * 10);
        format_trend(trend, sizeof(trend), s10, sp10);
        set_metric_card(0, val, trend, data->health.sleep >= 7.0 ? COLOR_GOOD : (data->health.sleep >= 6.0 ? COLOR_WARN : COLOR_BAD));

        // Steps
        snprintf(val, sizeof(val), "%d", data->health.steps);
        format_trend(trend, sizeof(trend), data->health.steps, data->health.steps_prev);
        set_metric_card(1, val, trend, data->health.steps >= data->health.steps_prev ? COLOR_GOOD : COLOR_BAD);

        // Heart Rate (live)
        snprintf(val, sizeof(val), ui_text("%d bpm"), data->health.hr);
        set_metric_card(2, val, "", data->health.hr <= 100 ? COLOR_GOOD : COLOR_WARN);

        // RHR
        snprintf(val, sizeof(val), ui_text("%d bpm"), data->health.rhr);
        set_metric_card(3, val, "", data->health.rhr <= 60 ? COLOR_GOOD : (data->health.rhr <= 75 ? COLOR_WARN : COLOR_BAD));

        // HRV
        snprintf(val, sizeof(val), "%d ms", data->health.hrv);
        set_metric_card(4, val, "", data->health.hrv >= 40 ? COLOR_GOOD : (data->health.hrv >= 25 ? COLOR_WARN : COLOR_BAD));

        // SpO2
        snprintf(val, sizeof(val), "%d%%", data->health.spo2);
        set_metric_card(5, val, "", data->health.spo2 >= 95 ? COLOR_GOOD : COLOR_WARN);
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
    if (room >= 0 && room < MAX_ROOMS && light >= 0 && light < MAX_ROOM_LIGHTS &&
        room_light_entity_ids[room][light][0]) {
        request_home_action(room_light_entity_ids[room][light], room_light_on[room][light] ? "turn_off" : "turn_on", 0, NULL);
        home_feedback_tick(NULL);
    }
}

static void favorite_btn_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (!ui_has_preferences || i < 0 || i >= ui_preferences.favorite_count || !favorite_available[i]) return;
    const web_favorite_t &f = ui_preferences.favorites[i];
    if (!strcmp(f.kind, "climate")) {
        if (home_climate_card) lv_obj_scroll_to_view(home_climate_card, LV_ANIM_ON);
    } else {
        request_home_action(f.entity_id, favorite_on[i] ? "turn_off" : "turn_on", 0, NULL);
        home_feedback_tick(NULL);
    }
}

static void ota_btn_cb(lv_event_t *e)
{
    (void)e;
    request_ota_check();
}



static void climate_power_btn_cb(lv_event_t *e)
{
    (void)e;
    if (!home_climate_entity[0]) return;
    home_temp_proposed = false;
    request_home_action(home_climate_entity, home_climate_on ? "turn_off" : "turn_on", 0, NULL);
    home_feedback_tick(NULL);
}

static void climate_temperature_btn_cb(lv_event_t *e)
{
    int delta = (int)(intptr_t)lv_event_get_user_data(e);
    if (!home_climate_entity[0]) return;
    home_climate_target_value += delta;
    if (home_climate_target_value < 16) home_climate_target_value = 16;
    if (home_climate_target_value > 30) home_climate_target_value = 30;
    home_temp_proposed = true;
    home_temp_due_ms = (uint64_t)(esp_timer_get_time() / 1000) + 500;
    if (!wifi_is_connected()) { home_temp_proposed = false; request_home_action(home_climate_entity, "set_temperature", home_climate_target_value, NULL); }
    home_feedback_tick(NULL);
}

static void climate_mode_btn_cb(lv_event_t *e)
{
    const char *mode = (const char *)lv_event_get_user_data(e);
    if (!home_climate_entity[0] || !mode) return;
    home_temp_proposed = false;
    request_home_action(home_climate_entity, "set_hvac_mode", 0, mode);
    home_feedback_tick(NULL);
}

static bool feedback_blocks(const action_record_t *r)
{
    return r && (r->in_flight || (!r->timed_out && (r->phase == ACTION_PENDING || r->phase == ACTION_ACCEPTED || r->phase == ACTION_UNKNOWN)));
}
static const char *feedback_text(const action_record_t *r)
{
    if (!r) return "";
    if (r->timed_out && (r->phase == ACTION_PENDING || r->phase == ACTION_ACCEPTED || r->phase == ACTION_UNKNOWN)) return ui_text("Нет подтверждения");
    switch (r->phase) {
    case ACTION_PENDING: return ui_text("Отправка...");
    case ACTION_ACCEPTED: return ui_text("Проверка...");
    case ACTION_UNKNOWN: return r->error == ACTION_ERROR_TIMEOUT ? ui_text("Таймаут. Исход неизвестен") : ui_text("Исход неизвестен");
    case ACTION_CONFIRMED: return ui_text("Подтверждено");
    case ACTION_UPDATED: return ui_text("Состояние обновлено");
    case ACTION_REJECTED:
        if (r->error == ACTION_ERROR_UNAUTHORIZED) return ui_text("Нет доступа. Отклонено");
        if (r->error == ACTION_ERROR_UNSUPPORTED) return ui_text("Не поддерживается");
        return ui_text("Отклонено");
    case ACTION_NOT_SENT:
        if (r->error == ACTION_ERROR_OFFLINE) return ui_text("Нет Wi-Fi. Не отправлено");
        if (r->error == ACTION_ERROR_QUEUE_FULL) return ui_text("Очередь занята. Не отправлено");
        if (r->error == ACTION_ERROR_TIMEOUT || r->timed_out) return ui_text("Время истекло. Не отправлено");
        return ui_text("Не отправлено");
    default: return "";
    }
}
static lv_color_t feedback_color(const action_record_t *record)
{
    if (!record) return COLOR_TEXT_DIM;
    if (record->phase == ACTION_REJECTED || record->phase == ACTION_NOT_SENT) return COLOR_BAD;
    if (record->phase == ACTION_CONFIRMED || record->phase == ACTION_UPDATED) return COLOR_GOOD;
    return COLOR_WARN;
}
static void home_feedback_tick(lv_timer_t *)
{
    uint64_t now = (uint64_t)(esp_timer_get_time() / 1000);
    home_action_tick();
    if (home_temp_proposed && now >= home_temp_due_ms) {
        home_temp_proposed = false;
        request_home_action(home_climate_entity, "set_temperature", home_climate_target_value, NULL);
    }
    for (int r = 0; r < MAX_ROOMS; r++) for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
        if (!room_light_btns[r][l] || !room_light_feedback[r][l]) continue;
        const action_record_t *record = home_action_find(room_light_entity_ids[r][l]);
        lv_label_set_text(room_light_feedback[r][l], feedback_text(record));
        lv_obj_set_style_text_color(room_light_feedback[r][l], feedback_color(record), 0);
        if (feedback_blocks(record)) lv_obj_add_state(room_light_btns[r][l], LV_STATE_DISABLED);
        else lv_obj_remove_state(room_light_btns[r][l], LV_STATE_DISABLED);
    }
    for (int i = 0; i < 4; ++i) {
        if (!favorite_buttons[i]) continue;
        const action_record_t *f = i < ui_preferences.favorite_count ? home_action_find(ui_preferences.favorites[i].entity_id) : NULL;
        const char *status = !favorite_available[i] ? ui_text("Нет данных") :
            (i < ui_preferences.favorite_count && !strcmp(ui_preferences.favorites[i].kind, "climate") ?
             ui_text("Открыть управление") : ui_text(favorite_on[i] ? "Включено" : "Выключено"));
        if (f && feedback_text(f)[0]) status = feedback_text(f);
        lv_label_set_text(favorite_feedback[i], status);
        lv_obj_set_style_text_color(favorite_feedback[i], feedback_color(f), 0);
        if (!favorite_available[i] || feedback_blocks(f)) lv_obj_add_state(favorite_buttons[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(favorite_buttons[i], LV_STATE_DISABLED);
    }
    const action_record_t *record = home_action_find(home_climate_entity);
    bool selecting = home_temp_proposed || (record && !strcmp(record->action, "set_temperature") && feedback_blocks(record));
    if (!home_temp_proposed && !feedback_blocks(record)) home_climate_target_value = home_temp_confirmed;
    if (home_climate_selected) {
        char text[48] = {};
        if (selecting) snprintf(text, sizeof(text), ui_text("Выбрано %.0f C"), home_climate_target_value);
        lv_label_set_text(home_climate_selected, text);
    }
    if (home_climate_feedback) {
        lv_label_set_text(home_climate_feedback, record ? feedback_text(record) : (home_climate_known ? "" : ui_text("Нет данных")));
        lv_obj_set_style_text_color(home_climate_feedback, feedback_color(record), 0);
    }
    bool disabled = !home_climate_known || feedback_blocks(record);
    if (home_climate_power) { if (disabled) lv_obj_add_state(home_climate_power, LV_STATE_DISABLED); else lv_obj_remove_state(home_climate_power, LV_STATE_DISABLED); }
    for (int i=0;i<4;i++) if (home_climate_mode_btns[i]) { if (disabled) lv_obj_add_state(home_climate_mode_btns[i], LV_STATE_DISABLED); else lv_obj_remove_state(home_climate_mode_btns[i], LV_STATE_DISABLED); }
    for (int i=0;i<2;i++) if (home_climate_temp_buttons[i]) { if (disabled) lv_obj_add_state(home_climate_temp_buttons[i], LV_STATE_DISABLED); else lv_obj_remove_state(home_climate_temp_buttons[i], LV_STATE_DISABLED); }
}

static bool settings_is_active(void)
{
    return tileview && nav_tiles[SETTINGS_PAGE] &&
           lv_tileview_get_tile_active(tileview) == nav_tiles[SETTINGS_PAGE];
}

void ui_dashboard_hide_sensitive(void)
{
    settings_reveal_until_us = 0;
    if (settings_password) lv_label_set_text(settings_password, "****************");
    if (settings_reveal_label) set_static_ui_label(settings_reveal_label, "Показать пароль");
}

static void settings_privacy_tick(lv_timer_t *)
{
    if (!settings_is_active() || ui_screen_off) ui_dashboard_cancel_brightness_test();
    if (settings_is_active()) refresh_reference_test();
    if (settings_reveal_until_us && (!settings_is_active() || ui_screen_off ||
        esp_timer_get_time() >= settings_reveal_until_us)) ui_dashboard_hide_sensitive();
}

void ui_dashboard_open_settings(void)
{
    ui_dashboard_hide_sensitive();
    if (tileview && nav_tiles[SETTINGS_PAGE])
        lv_tileview_set_tile(tileview, nav_tiles[SETTINGS_PAGE], LV_ANIM_OFF);
}

// The Bridge contract supplies only a public settings URL and optional device ID.
// Fail closed on credentials, fragments or unrelated query parameters before
// putting anything into a scannable QR or a visible label.
static bool public_settings_url(const char *url)
{
    if (!url || strlen(url) >= sizeof(settings_qr_url)) return false;
    const char *host = !strncmp(url, "https://", 8) ? url + 8 :
                       !strncmp(url, "http://", 7) ? url + 7 : nullptr;
    if (!host || !*host || strchr(host, '@') || strchr(host, '#')) return false;
    const char *path = strchr(host, '/');
    if (!path || path == host || strncmp(path, "/settings/", 10)) return false;
    for (const char *p = url; *p; ++p) if (static_cast<unsigned char>(*p) <= 0x20) return false;
    const char *query = path + 10;
    if (!*query) return true;
    if (strncmp(query, "?device_id=", 11)) return false;
    query += 11;
    if (strlen(query) != 32) return false;
    for (; *query; ++query) if (!((*query >= '0' && *query <= '9') || (*query >= 'a' && *query <= 'f'))) return false;
    return true;
}

static void refresh_settings(void)
{
    if (!settings_web_link) return;
    const char *url = ui_has_preferences ? ui_preferences.settings_url : "";
    if (public_settings_url(url)) {
        lv_label_set_text(settings_web_link, url);
        if (strcmp(settings_qr_url, url)) {
            if (lv_qrcode_update(settings_qr, url, strlen(url)) == LV_RESULT_OK) {
                snprintf(settings_qr_url, sizeof(settings_qr_url), "%s", url);
                lv_obj_clear_flag(settings_qr, LV_OBJ_FLAG_HIDDEN);
            } else {
                settings_qr_url[0] = 0;
                lv_obj_add_flag(settings_qr, LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        settings_qr_url[0] = 0;
        lv_obj_add_flag(settings_qr, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(settings_web_link, ui_text("Откройте веб-панель Bridge в браузере"));
    }
    if (ui_recovery_active) {
        char network[96];
        snprintf(network, sizeof(network), ui_text("Сеть: %s"), ui_recovery_ssid);
        lv_label_set_text(settings_ap_name, network);
        lv_label_set_text(settings_ap_url, ui_recovery_url);
        lv_obj_clear_flag(settings_reveal_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(settings_password, LV_OBJ_FLAG_HIDDEN);
        set_static_ui_label(settings_ap_button_label, "Выключить сеть");
        set_static_ui_label(settings_ap_help, "Пароль открывается на 15 секунд по нажатию.");
    } else {
        ui_dashboard_hide_sensitive();
        lv_label_set_text(settings_ap_name, ui_text("Временная сеть выключена"));
        lv_label_set_text(settings_ap_url, "");
        lv_obj_add_flag(settings_reveal_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(settings_password, LV_OBJ_FLAG_HIDDEN);
        set_static_ui_label(settings_ap_button_label, "Включить на 10 минут");
        set_static_ui_label(settings_ap_help, "Для восстановления Wi-Fi или связи с Bridge.");
    }
    char status[256], sync[96] = {}, data[64] = {};
    web_sync_copy_diagnostic(sync, sizeof(sync));
    bridge_copy_last_error(data, sizeof(data));
    const bool bridge_online = wifi_is_connected() && !strcmp(sync, "Sync:OK") &&
        (!strcmp(data, "OK") || (!strcmp(data, "Fetching...") && bridge_has_data()));
    snprintf(status, sizeof(status), ui_text("Wi-Fi: %s\nBridge: %s"),
             ui_text(wifi_is_connected() ? "Подключён" : "Нет связи"),
             ui_text(bridge_online ? "На связи" : "Нет связи"));
    if (settings_device_status) lv_label_set_text(settings_device_status, status);
    refresh_reference_test();
}

void ui_dashboard_cancel_brightness_test(void) {
    if (!settings_reference_panel || lv_obj_has_flag(settings_reference_panel, LV_OBJ_FLAG_HIDDEN)) return;
    brightness_runtime_reference_end();
    lv_obj_add_flag(settings_reference_panel, LV_OBJ_FLAG_HIDDEN);
}
static void reference_close_cb(lv_event_t *) { ui_dashboard_cancel_brightness_test(); }
static void reference_open_cb(lv_event_t *) {
    if (!settings_reference_panel) return;
    ui_dashboard_hide_sensitive();
    lv_obj_clear_flag(settings_reference_panel, LV_OBJ_FLAG_HIDDEN);
    refresh_reference_test();
}
static void enable_calibration_button(lv_obj_t *button, bool enabled) {
    lv_obj_set_style_opa(button, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
    if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
}
static void refresh_reference_test()
{
    if (!settings_reference_status) return;
    auto status = brightness_runtime_reference_status();
    auto c = brightness_runtime_comparison();
    settings_rendered_comparison = c;
    char text[256];
    const char *message = "Начните с исходной 50. Команды — не проценты.";
    const char *button = "Начать: исходная 50";
    if (status.phase == BRIGHTNESS_REFERENCE_DISABLED) {
        message = "Тест подсветки недоступен";
    } else if (status.phase == BRIGHTNESS_REFERENCE_CONFIRM && !c.armed) {
        message = "Исходная 50 установлена. Экран читается?";
        button = "Экран читается — начать";
    } else if (status.phase == BRIGHTNESS_REFERENCE_ERROR) {
        message = "Ошибка возврата. Новые тесты заблокированы."; button = "Вернуть сейчас";
    } else if (c.armed) {
        snprintf(text, sizeof(text), ui_text("Сейчас %u. Возврат на 50 через %u с."), (unsigned)c.current, (unsigned)status.remaining_seconds);
        message = text; button = "Вернуть 50";
        if (status.phase == BRIGHTNESS_REFERENCE_CONFIRM) message = "Выберите шаг. Возврат — через 20 с после нажатия.";
    } else if (c.historical) {
        message = "Возвращено 50. Ниже — сравнение завершённого теста.";
    } else if (c.step_failed) message = "Ошибка шага. Сравнение отменено. Начните заново.";
    lv_label_set_text(settings_reference_status, ui_text(message));
    set_static_ui_label(settings_reference_label, button);
    enable_calibration_button(settings_reference_button, status.phase != BRIGHTNESS_REFERENCE_DISABLED);
    if (c.valid) {
        if (c.has_previous) snprintf(text, sizeof(text), ui_text("Предыдущий: %u · Тест: %u"), (unsigned)c.previous, (unsigned)c.current);
        else snprintf(text, sizeof(text), ui_text("Текущий уровень: %u. Предыдущего нет."), (unsigned)c.current);
    } else snprintf(text, sizeof(text), "%s", ui_text("Сравнение ещё не начато"));
    lv_label_set_text(settings_comparison_label, text);
    char bright[8] = "—", dark[8] = "—";
    if (c.has_bright) snprintf(bright, sizeof(bright), "%u", (unsigned)c.bright);
    if (c.has_dark) snprintf(dark, sizeof(dark), "%u", (unsigned)c.dark);
    snprintf(text, sizeof(text), ui_text("Выбрано · яркий: %s · тёмный: %s"), bright, dark);
    lv_label_set_text(settings_result_label, text);
    snprintf(text, sizeof(text), ui_text("Шаг: %u (нажмите для смены)"), (unsigned)settings_step_size);
    lv_label_set_text(settings_step_size_label, text);
    set_static_ui_label(settings_target_label, settings_dark_target ? "Выбираем тёмный уровень" : "Выбираем яркий уровень");
    for (int i = 0; i < 2; ++i) {
        enable_calibration_button(settings_step_buttons[i], c.armed && (i == 0 ? c.current > 1 : c.current < 97));
        uint8_t chosen = i == 0 ? c.previous : c.current;
        bool order = settings_dark_target ? (!c.has_bright || chosen > c.bright) : (!c.has_dark || chosen < c.dark);
        enable_calibration_button(settings_mark_buttons[i], c.valid && (c.armed || c.historical) && (i != 0 || c.has_previous) && order);
    }
    const char *previous_key = settings_dark_target ? "Слишком темно — выбрать %u" : "Разницы нет — выбрать %u";
    if (c.has_previous) snprintf(text, sizeof(text), ui_text(previous_key), (unsigned)c.previous);
    else snprintf(text, sizeof(text), "%s", ui_text("Предыдущего уровня нет"));
    lv_label_set_text(settings_mark_labels[0], text);
    if (c.valid) snprintf(text, sizeof(text), ui_text("Этот подходит — выбрать %u"), (unsigned)c.current);
    else snprintf(text, sizeof(text), "%s", ui_text("Сначала начните сравнение"));
    lv_label_set_text(settings_mark_labels[1], text);
}
static void reference_test_cb(lv_event_t *) {
    auto status = brightness_runtime_reference_status();
    auto c = brightness_runtime_comparison();
    if (status.phase == BRIGHTNESS_REFERENCE_IDLE || status.phase == BRIGHTNESS_REFERENCE_RESTORED)
        brightness_runtime_reference_baseline();
    else if (status.phase == BRIGHTNESS_REFERENCE_CONFIRM && !c.armed)
        brightness_runtime_reference_begin();
    else brightness_runtime_reference_end();
    refresh_reference_test();
}
static void reference_choice_cb(lv_event_t *event) {
    int index = (int)(intptr_t)lv_event_get_user_data(event);
    auto &action = calibration_actions[index];
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        action = {settings_rendered_comparison.id, settings_rendered_comparison.historical,
                  settings_dark_target, settings_step_size};
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (index < 2) brightness_runtime_reference_step(action.id, index == 0, action.step);
    else brightness_runtime_reference_mark(action.id, index == 2, action.dark, action.historical);
    refresh_reference_test();
}
static void reference_step_size_cb(lv_event_t *) { settings_step_size = settings_step_size == 5 ? 1 : 5; refresh_reference_test(); }
static void reference_target_cb(lv_event_t *) { settings_dark_target = !settings_dark_target; refresh_reference_test(); }

static void reveal_password_cb(lv_event_t *)
{
    if (!ui_recovery_active || !settings_is_active() || ui_screen_off) return;
    if (settings_reveal_until_us) { ui_dashboard_hide_sensitive(); return; }
    settings_reveal_until_us = esp_timer_get_time() + 15LL * 1000000;
    lv_label_set_text(settings_password, ui_recovery_password);
    set_static_ui_label(settings_reveal_label, "Скрыть пароль");
}

static void recovery_toggle_cb(lv_event_t *)
{
    ui_dashboard_hide_sensitive();
    if (ui_recovery_active) request_stop_recovery_ap();
    else request_recovery_ap();
}

static lv_obj_t *settings_text(lv_obj_t *parent, int x, int y, int width, int height,
                                const char *key, bool large = false)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, large ? &font_montserrat_24_cyr : &font_montserrat_16_cyr, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    if (key && key[0]) set_static_ui_label(label, key);
    else lv_label_set_text(label, "");
    return label;
}

static lv_obj_t *settings_button(lv_obj_t *parent, int x, int y, int width,
                                  const char *key, lv_event_cb_t callback, lv_obj_t **out_label = nullptr)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 44);
    lv_obj_set_style_bg_color(button, COLOR_ACCENT, 0);
    lv_obj_set_style_radius(button, 20, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *label = settings_text(button, 0, 0, width - 16, 24, key);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    if (out_label) *out_label = label;
    return button;
}

static void create_settings_page(lv_obj_t *tile)
{
    settings_text(tile, 18, 12, 980, 34, "Настройки", true);
    settings_text(tile, 18, 48, 980, 24, "Веб-панель и локальное восстановление");
    lv_obj_t *web = make_card(tile, 14, 80, 480, 270);
    settings_text(web, 16, 12, 438, 28, "Веб-панель Bridge", true);
    settings_text(web, 16, 52, 244, 60, "Экран, языки, фотографии и сервисы");
    settings_web_link = settings_text(web, 16, 130, 248, 76, "");
    settings_text(web, 16, 216, 440, 42, "Вход через пароль или Cloudflare");
    settings_qr = lv_qrcode_create(web);
    lv_qrcode_set_size(settings_qr, 164);
    lv_qrcode_set_dark_color(settings_qr, COLOR_TEXT);
    lv_qrcode_set_light_color(settings_qr, COLOR_CARD);
    lv_obj_set_pos(settings_qr, 284, 52);
    lv_obj_add_flag(settings_qr, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *local = make_card(tile, 508, 80, 502, 270);
    settings_text(local, 16, 12, 470, 28, "Локальное восстановление", true);
    settings_ap_name = settings_text(local, 16, 58, 470, 26, "");
    settings_ap_url = settings_text(local, 16, 90, 470, 24, "");
    settings_password = settings_text(local, 16, 120, 470, 30, "");
    settings_reveal_btn = settings_button(local, 16, 158, 214, "Показать пароль", reveal_password_cb, &settings_reveal_label);
    settings_button(local, 242, 158, 242, "Включить на 10 минут", recovery_toggle_cb, &settings_ap_button_label);
    settings_ap_help = settings_text(local, 16, 214, 470, 44, "");

    lv_obj_t *system = make_card(tile, 14, 364, 996, 160);
    if (HOMEDASH_BRIGHTNESS_REFERENCE_TEST) {
        settings_text(system, 16, 12, 930, 28, "Тест подсветки", true);
        settings_text(system, 16, 54, 570, 80, "Пошаговый подбор с возвратом на 50");
        settings_button(system, 620, 46, 350, "Подбор подсветки", reference_open_cb);
        settings_button(system, 620, 100, 350, "Проверить обновление", ota_btn_cb);
        settings_reference_panel = make_card(tile, 14, 8, 996, 516);
        lv_obj_clear_flag(settings_reference_panel, LV_OBJ_FLAG_SCROLLABLE);
        settings_text(settings_reference_panel, 20, 12, 750, 36, "Подбор подсветки", true);
        settings_button(settings_reference_panel, 814, 10, 156, "Назад", reference_close_cb);
        settings_reference_status = settings_text(settings_reference_panel, 20, 66, 950, 48, "");
        settings_comparison_label = settings_text(settings_reference_panel, 20, 116, 950, 36, "", true);
        settings_result_label = settings_text(settings_reference_panel, 20, 160, 950, 24, "");
        settings_button(settings_reference_panel, 20, 192, 462, "", reference_step_size_cb, &settings_step_size_label);
        settings_button(settings_reference_panel, 508, 192, 462, "", reference_target_cb, &settings_target_label);
        for (int i = 0; i < 4; ++i) {
            lv_obj_t *label;
            lv_obj_t *button = settings_button(settings_reference_panel, i < 2 ? 20 + i * 488 : 20,
                i < 2 ? 250 : 308 + (i-2)*54, i < 2 ? 462 : 950, i == 0 ? "Ярче" : i == 1 ? "Темнее" : "", reference_choice_cb, &label);
            lv_obj_remove_event_cb(button, reference_choice_cb);
            lv_obj_add_event_cb(button, reference_choice_cb, LV_EVENT_PRESSED, (void *)(intptr_t)i);
            lv_obj_add_event_cb(button, reference_choice_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            if (i < 2) { settings_step_buttons[i] = button; settings_step_labels[i] = label; }
            else { settings_mark_buttons[i-2] = button; settings_mark_labels[i-2] = label; }
        }
        settings_reference_button = settings_button(settings_reference_panel, 20, 424, 950, "", reference_test_cb, &settings_reference_label);
        settings_text(settings_reference_panel, 20, 480, 950, 24, "20 с без нажатий · сессия до 2 минут · выбор до перезагрузки");
        lv_obj_add_flag(settings_reference_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        settings_text(system, 16, 12, 930, 28, "Устройство", true);
        settings_device_status = settings_text(system, 16, 54, 570, 80, "");
        settings_button(system, 620, 64, 350, "Проверить обновление", ota_btn_cb);
    }
    ui_dashboard_hide_sensitive();
    refresh_settings();
}

static void create_page3(lv_obj_t *tile)
{
    page3_root = tile;
    lv_obj_set_scroll_dir(tile, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(tile, LV_SCROLLBAR_MODE_AUTO);
    // Title
    lv_obj_t *pg_title = lv_label_create(tile);
    lv_obj_set_style_text_color(pg_title, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(pg_title, &font_montserrat_24_cyr, 0);
    set_static_ui_label(pg_title, "Дом");
    lv_obj_set_width(pg_title, 180);
    lv_obj_set_pos(pg_title, 15, 8);



    for (int i = 0; i < 4; ++i) {
        lv_obj_t *btn = lv_btn_create(tile);
        lv_obj_set_pos(btn, 5 + i * 253, 55); lv_obj_set_size(btn, 245, 76);
        lv_obj_set_style_bg_color(btn, COLOR_LIGHT_OFF, 0); lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_opa(btn, LV_OPA_80, LV_STATE_DISABLED);
        lv_obj_add_event_cb(btn, favorite_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        favorite_buttons[i] = btn;
        favorite_labels[i] = lv_label_create(btn); lv_obj_set_size(favorite_labels[i], 215, 24);
        lv_obj_set_style_text_font(favorite_labels[i], &font_montserrat_16_cyr, 0);
        lv_obj_set_style_text_color(favorite_labels[i], lv_color_hex(0x1A1A2E), 0);
        lv_label_set_long_mode(favorite_labels[i], LV_LABEL_LONG_SCROLL_CIRCULAR); lv_obj_set_pos(favorite_labels[i], 0, 0);
        favorite_feedback[i] = lv_label_create(btn); lv_obj_set_size(favorite_feedback[i], 215, 26);
        lv_obj_set_style_text_font(favorite_feedback[i], &font_montserrat_16_cyr, 0);
        lv_label_set_long_mode(favorite_feedback[i], LV_LABEL_LONG_DOT); lv_obj_set_pos(favorite_feedback[i], 0, 28);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    }
    // Air overview: the two CO2 sensors remain explicitly named, rather than
    // collapsing household air into an ambiguous sensor count.
    lv_obj_t *air = make_card(tile, 5, 60, 400, 176);
    home_air_card = air;
    lv_obj_t *air_title = lv_label_create(air);
    lv_obj_set_style_text_color(air_title, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(air_title, &font_montserrat_16_cyr, 0);
    set_static_ui_label(air_title, "Воздух дома");
    lv_obj_set_width(air_title, 370);
    lv_obj_set_pos(air_title, 14, 10);
    const char *air_names[] = {"Гостиная CO2", "Кабинет CO2", "PM2.5", "Влажность"};
    for (int i = 0; i < 4; i++) {
        int col = i % 2, row = i / 2;
        home_air_values[i] = lv_label_create(air);
        lv_obj_set_style_text_color(home_air_values[i], COLOR_TEXT, 0);
        lv_obj_set_style_text_font(home_air_values[i], &font_montserrat_16_cyr, 0);
        lv_label_set_text(home_air_values[i], "---");
        lv_obj_set_pos(home_air_values[i], 14 + col * 190, 32 + row * 68);
        lv_obj_set_size(home_air_values[i], 174, 22);
        lv_label_set_long_mode(home_air_values[i], LV_LABEL_LONG_DOT);
        home_air_names[i] = lv_label_create(air);
        lv_obj_set_style_text_color(home_air_names[i], COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(home_air_names[i], &font_montserrat_16_cyr, 0);
        set_static_ui_label(home_air_names[i], air_names[i]);
        lv_obj_set_pos(home_air_names[i], 14 + col * 190, 54 + row * 68);
        lv_obj_set_size(home_air_names[i], 174, 44);
        lv_label_set_long_mode(home_air_names[i], LV_LABEL_LONG_DOT);
    }

    lv_obj_t *climate = make_card(tile, 412, 60, 607, 176);
    home_climate_card = climate;
    home_climate_name = lv_label_create(climate);
    lv_obj_set_style_text_color(home_climate_name, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_text_font(home_climate_name, &font_montserrat_16_cyr, 0);
    set_static_ui_label(home_climate_name, "Кондиционер");
    lv_obj_set_pos(home_climate_name, 14, 10);
    lv_obj_set_size(home_climate_name, 570, 24);
    lv_label_set_long_mode(home_climate_name, LV_LABEL_LONG_DOT);
    home_climate_feedback = lv_label_create(climate);
    lv_obj_set_pos(home_climate_feedback, 380, 80);
    lv_obj_set_size(home_climate_feedback, 210, 44);
    lv_obj_set_style_text_font(home_climate_feedback, &font_montserrat_16_cyr, 0);
    lv_label_set_long_mode(home_climate_feedback, LV_LABEL_LONG_WRAP);
    lv_label_set_text(home_climate_feedback, "");
    home_climate_selected = lv_label_create(climate);
    lv_obj_set_pos(home_climate_selected, 174, 80);
    lv_obj_set_size(home_climate_selected, 198, 44);
    lv_obj_set_style_text_font(home_climate_selected, &font_montserrat_16_cyr, 0);
    lv_label_set_long_mode(home_climate_selected, LV_LABEL_LONG_WRAP);
    lv_label_set_text(home_climate_selected, "");
    home_climate_current = lv_label_create(climate);
    lv_obj_set_style_text_color(home_climate_current, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(home_climate_current, &font_montserrat_16_cyr, 0);
    set_static_ui_label(home_climate_current, "Сейчас ---");
    lv_obj_set_pos(home_climate_current, 14, 46);
    lv_obj_set_size(home_climate_current, 156, 28);
    lv_label_set_long_mode(home_climate_current, LV_LABEL_LONG_DOT);
    home_climate_target = lv_label_create(climate);
    lv_obj_set_style_text_color(home_climate_target, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(home_climate_target, &font_montserrat_16_cyr, 0);
    set_static_ui_label(home_climate_target, "Цель ---");
    lv_obj_set_pos(home_climate_target, 14, 82);
    lv_obj_set_size(home_climate_target, 156, 28);
    lv_label_set_long_mode(home_climate_target, LV_LABEL_LONG_DOT);
    home_climate_power = lv_btn_create(climate);
    lv_obj_set_size(home_climate_power, 135, 36);
    lv_obj_set_pos(home_climate_power, 174, 42);
    lv_obj_set_style_radius(home_climate_power, 18, 0);
    lv_obj_set_style_shadow_width(home_climate_power, 0, 0);
    lv_obj_set_style_opa(home_climate_power, LV_OPA_80, LV_STATE_DISABLED);
    lv_obj_add_event_cb(home_climate_power, climate_power_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *power_label = lv_label_create(home_climate_power);
    lv_obj_set_style_text_color(power_label, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(power_label, &font_montserrat_16_cyr, 0);
    lv_obj_set_width(power_label, 125);
    set_static_ui_label(power_label, "Вкл / выкл");
    lv_obj_center(power_label);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *btn = lv_btn_create(climate);
        home_climate_temp_buttons[i] = btn;
        lv_obj_set_style_opa(btn, LV_OPA_80, LV_STATE_DISABLED);
        lv_obj_set_size(btn, 42, 36);
        lv_obj_set_pos(btn, 320 + i * 48, 42);
        lv_obj_set_style_radius(btn, 18, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, climate_temperature_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(i ? 1 : -1));
        lv_obj_t *label = lv_label_create(btn);
        lv_obj_set_style_text_font(label, &font_montserrat_24_cyr, 0);
        lv_label_set_text(label, i ? "+" : "-");
        lv_obj_center(label);
    }
    static const char *modes[] = {"cool", "heat", "dry", "fan_only"};
    static const char *mode_labels[] = {"Охлаждение", "Обогрев", "Осушение", "Вентиляция"};
    for (int i = 0; i < 4; i++) {
        home_climate_mode_btns[i] = lv_btn_create(climate);
        lv_obj_set_style_opa(home_climate_mode_btns[i], LV_OPA_80, LV_STATE_DISABLED);
        lv_obj_set_size(home_climate_mode_btns[i], 136, 36);
        lv_obj_set_pos(home_climate_mode_btns[i], 14 + i * 145, 128);
        lv_obj_set_style_radius(home_climate_mode_btns[i], 15, 0);
        lv_obj_set_style_shadow_width(home_climate_mode_btns[i], 0, 0);
        lv_obj_add_event_cb(home_climate_mode_btns[i], climate_mode_btn_cb, LV_EVENT_CLICKED, (void *)modes[i]);
        lv_obj_t *label = lv_label_create(home_climate_mode_btns[i]);
        lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(label, &font_montserrat_16_cyr, 0);
        lv_obj_set_width(label, 130);
        set_static_ui_label(label, mode_labels[i]);
        lv_obj_center(label);
    }

    // Compact 2x2 light grid. Air detail now lives in the persistent overview
    // above, leaving each room one clear action area.
    int pw = 500, ph = 140;
    int positions[MAX_ROOMS][2] = {
        {5, 246}, {512, 246}, {5, 394}, {512, 394}
    };

    for (int r = 0; r < MAX_ROOMS; r++) {
        const room_def_t *room = &ROOMS[r];
        lv_obj_t *panel = make_card(tile, positions[r][0], positions[r][1], pw, ph);
        home_room_cards[r] = panel;

        // Room title
        lv_obj_t *lbl_title = lv_label_create(panel);
        lv_obj_set_style_text_color(lbl_title, COLOR_HIGHLIGHT, 0);
        lv_obj_set_style_text_font(lbl_title, &font_montserrat_24_cyr, 0);
        lv_label_set_text(lbl_title, room->name);
        lv_obj_set_pos(lbl_title, 15, 8);

        // Light toggle buttons
        for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
            lv_obj_t *btn = lv_btn_create(panel);
            lv_obj_set_size(btn, 148, 38);
            lv_obj_set_pos(btn, 15 + l * 156, 48);
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

            room_light_feedback[r][l] = lv_label_create(panel);
            lv_obj_set_pos(room_light_feedback[r][l], 15 + l * 156, 90);
            lv_obj_set_size(room_light_feedback[r][l], 148, 44);
            lv_obj_set_style_text_font(room_light_feedback[r][l], &font_montserrat_16_cyr, 0);
            lv_label_set_long_mode(room_light_feedback[r][l], LV_LABEL_LONG_WRAP);
            lv_label_set_text(room_light_feedback[r][l], "");
            lv_obj_set_style_opa(btn, LV_OPA_80, LV_STATE_DISABLED);
            room_light_btns[r][l] = btn;
            room_light_labels[r][l] = lbl;

            lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        }

    }
}

// Find light/sensor in bridge data by entity_id
static const bridge_light_t *find_light(const bridge_data_t *d, const char *eid)
{
    int count = d->light_count;
    if (count > BRIDGE_MAX_LIGHTS) count = BRIDGE_MAX_LIGHTS;
    if (count < 0) count = 0;
    for (int i = 0; i < count; i++) {
        if (strcmp(d->lights[i].entity_id, eid) == 0) return &d->lights[i];
    }
    return NULL;
}

static void bind_light_slot(int room, int slot, const bridge_light_t *light)
{
    if (!light || room < 0 || room >= MAX_ROOMS || slot < 0 || slot >= MAX_ROOM_LIGHTS) return;
    snprintf(room_light_entity_ids[room][slot], sizeof(room_light_entity_ids[room][slot]), "%s", light->entity_id);
    room_light_on[room][slot] = light->on;
    const char *label = light->name[0] ? light->name : light->entity_id;
    lv_label_set_text(room_light_labels[room][slot], label);
    lv_obj_set_style_bg_color(room_light_btns[room][slot], light->on ? COLOR_LIGHT_ON : COLOR_LIGHT_OFF, 0);
    lv_obj_set_style_text_color(room_light_labels[room][slot], light->on ? lv_color_hex(0x1A1A2E) : COLOR_TEXT, 0);
    lv_obj_clear_flag(room_light_btns[room][slot], LV_OBJ_FLAG_HIDDEN);
}

static bool sensor_is_living_co2(const bridge_sensor_t *sensor)
{
    return sensor && strstr(sensor->name, "CO2") &&
           (strstr(sensor->name, "гостиная") || strstr(sensor->name, "Гостиная"));
}

static bool sensor_is_office_co2(const bridge_sensor_t *sensor)
{
    return sensor && strstr(sensor->name, "CO2") &&
           (strstr(sensor->name, "кабинет") || strstr(sensor->name, "Кабинет"));
}

static bool sensor_value_available(const char *value)
{
    if (!value || !value[0]) return false;
    char normalized[16] = {};
    size_t n = strlen(value);
    if (n >= sizeof(normalized)) return false;
    for (size_t i = 0; i < n; ++i) normalized[i] = std::tolower(static_cast<unsigned char>(value[i]));
    return strcmp(normalized, "unavailable") && strcmp(normalized, "unknown") &&
           strcmp(normalized, "nan") && strcmp(normalized, "none");
}

static lv_color_t co2_color(const char *value)
{
    char *end = nullptr;
    float co2 = strtof(value, &end);
    if (end == value || *end || !std::isfinite(co2) || co2 <= 0) return COLOR_TEXT_DIM;
    return co2 < 800 ? COLOR_GOOD : co2 < 1000 ? COLOR_WARN : COLOR_BAD;
}

static const char *air_sensor_label(const bridge_sensor_t *sensor, int index)
{
    if (sensor_is_living_co2(sensor)) return ui_text("Гостиная CO2");
    if (sensor_is_office_co2(sensor)) return ui_text("Кабинет CO2");
    if (sensor && strcmp(sensor->name, "PM2.5") == 0) return "PM2.5";
    if (sensor && strcmp(sensor->name, "Влажность") == 0) return ui_text("Влажность");
    if (sensor && sensor->name[0]) return sensor->name;
    static char fallback_labels[4][24];
    char *fallback = fallback_labels[index % 4];
        snprintf(fallback, sizeof(fallback_labels[0]), ui_text("Датчик %d"), index + 1);
    return fallback;
}

static const char *air_sensor_location(const bridge_sensor_t *sensor)
{
    if (sensor_is_living_co2(sensor)) return ui_text("Гостиная");
    if (sensor_is_office_co2(sensor)) return ui_text("Кабинет");
    return sensor && sensor->name[0] ? sensor->name : ui_text("Датчик");
}

void ui_dashboard_update_ha(const bridge_data_t *data)
{
    if (!data) return;
    if (data != &ui_bridge_snapshot) ui_bridge_snapshot = *data;

    if (lbl_air_primary && lbl_air_detail) {
        const bridge_sensor_t *living_room_co2 = NULL;
        const bridge_sensor_t *office_co2 = NULL;
        if (data->sensors_valid) {
            int count = data->sensor_count;
            if (count > BRIDGE_MAX_SENSORS) count = BRIDGE_MAX_SENSORS;
            if (count < 0) count = 0;
            for (int i = 0; i < count; i++) {
                const bridge_sensor_t *sensor = &data->sensors[i];
                if (!strstr(sensor->unit, "ppm")) continue;

                if (!living_room_co2) {
                    living_room_co2 = sensor;
                } else if (!office_co2) {
                    office_co2 = sensor;
                }
            }
        }

        if (living_room_co2 && sensor_value_available(living_room_co2->value)) {
            char text[48];
            snprintf(text, sizeof(text), "%s: CO2 %s %s", air_sensor_location(living_room_co2), living_room_co2->value, living_room_co2->unit);
            unbind_static_ui_label(lbl_air_primary);
            lv_label_set_text(lbl_air_primary, text);
            lv_obj_set_style_text_color(lbl_air_primary,
                co2_color(living_room_co2->value), 0);
        } else {
            set_static_ui_label(lbl_air_primary, "Гостиная: нет данных CO2");
            lv_obj_set_style_text_color(lbl_air_primary, COLOR_TEXT_DIM, 0);
        }

        if (office_co2 && sensor_value_available(office_co2->value)) {
            char text[48];
            snprintf(text, sizeof(text), "%s: CO2 %s %s", air_sensor_location(office_co2), office_co2->value, office_co2->unit);
            unbind_static_ui_label(lbl_air_detail);
            lv_label_set_text(lbl_air_detail, text);
            lv_obj_set_style_text_color(lbl_air_detail,
                co2_color(office_co2->value), 0);
        } else {
            set_static_ui_label(lbl_air_detail, "Кабинет: нет данных CO2");
            lv_obj_set_style_text_color(lbl_air_detail, COLOR_TEXT_DIM, 0);
        }
    }

    int shift = ui_has_preferences && ui_preferences.favorite_count ? 84 : 0;
    if (home_air_card) lv_obj_set_y(home_air_card, 60 + shift);
    if (home_climate_card) lv_obj_set_y(home_climate_card, 60 + shift);
    for (int r = 0; r < MAX_ROOMS; ++r) if (home_room_cards[r]) {
        lv_obj_set_y(home_room_cards[r], shift ? 330 + (r / 2) * 105 : 246 + (r / 2) * 148);
        lv_obj_set_height(home_room_cards[r], shift ? 100 : 140);
        for (int l = 0; l < MAX_ROOM_LIGHTS; ++l) {
            if (room_light_btns[r][l]) lv_obj_set_y(room_light_btns[r][l], shift ? 40 : 48);
            if (room_light_feedback[r][l]) { lv_obj_set_y(room_light_feedback[r][l], shift ? 80 : 90); lv_obj_set_height(room_light_feedback[r][l], shift ? 20 : 44); }
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!favorite_buttons[i]) continue;
        if (!ui_has_preferences || i >= ui_preferences.favorite_count) {
            favorite_available[i] = false; lv_obj_add_flag(favorite_buttons[i], LV_OBJ_FLAG_HIDDEN); continue;
        }
        const web_favorite_t &f = ui_preferences.favorites[i];
        const bridge_light_t *light = data->lights_valid ? find_light(data, f.entity_id) : NULL;
        bool is_light = !strcmp(f.kind, "light");
        const char *mode = data->climate_count > 0 ? data->climates[0].mode : "";
        bool climate_found = data->climate_valid && data->climate_count > 0 &&
            !strcmp(data->climates[0].entity_id, f.entity_id) &&
            (!strcmp(mode, "off") || !strcmp(mode, "cool") || !strcmp(mode, "heat") ||
             !strcmp(mode, "dry") || !strcmp(mode, "fan_only") || !strcmp(mode, "auto") || !strcmp(mode, "heat_cool"));
        favorite_available[i] = is_light ? light != NULL : climate_found;
        favorite_on[i] = is_light && light && light->on;
        const char *name = f.label[0] ? f.label : (is_light && light && light->name[0] ? light->name : f.entity_id);
        lv_label_set_text(favorite_labels[i], name);
        lv_obj_set_style_bg_color(favorite_buttons[i], favorite_on[i] ? COLOR_LIGHT_ON : COLOR_LIGHT_OFF, 0);
        lv_obj_clear_flag(favorite_buttons[i], LV_OBJ_FLAG_HIDDEN);
    }
    // Keep established slots for known entities. Additional selected lights
    // fill empty slots using the IDs and friendly names returned by Bridge.
    bool used_lights[BRIDGE_MAX_LIGHTS] = {};
    for (int r = 0; r < MAX_ROOMS; r++) {
        for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
            room_light_entity_ids[r][l][0] = '\0';
            if (!room_light_btns[r][l]) continue;
            lv_obj_add_flag(room_light_btns[r][l], LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(room_light_labels[r][l], "---");
            lv_obj_set_style_bg_color(room_light_btns[r][l], COLOR_LIGHT_OFF, 0);
            lv_obj_set_style_text_color(room_light_labels[r][l], COLOR_TEXT, 0);
        }
    }
    int light_count = data->lights_valid ? data->light_count : 0;
    if (light_count < 0) light_count = 0;
    if (light_count > BRIDGE_MAX_LIGHTS) light_count = BRIDGE_MAX_LIGHTS;
    if (data->lights_valid) {
        for (int r = 0; r < MAX_ROOMS; r++) {
            const room_def_t *room = &ROOMS[r];
            for (int l = 0; l < room->light_count; l++) {
                const bridge_light_t *light = find_light(data, room->light_ids[l]);
                if (!light) continue;
                int index = (int)(light - data->lights);
                if (index >= 0 && index < light_count) used_lights[index] = true;
                bind_light_slot(r, l, light);
            }
        }
    }
    for (int i = 0; i < light_count; i++) {
        if (used_lights[i] || !data->lights[i].entity_id[0]) continue;
        int target_room = -1, target_slot = -1;
        for (int r = 0; r < MAX_ROOMS && target_room < 0; r++) {
            for (int l = 0; l < MAX_ROOM_LIGHTS; l++) {
                if (!room_light_entity_ids[r][l][0]) {
                    target_room = r;
                    target_slot = l;
                    break;
                }
            }
        }
        if (target_room < 0) break;
        bind_light_slot(target_room, target_slot, &data->lights[i]);
    }

    int sensor_count = data->sensors_valid ? data->sensor_count : 0;
    if (sensor_count < 0) sensor_count = 0;
    if (sensor_count > BRIDGE_MAX_SENSORS) sensor_count = BRIDGE_MAX_SENSORS;
    static const char *default_air_names[] = {"Гостиная CO2", "Кабинет CO2", "PM2.5", "Влажность"};
    for (int i = 0; i < 4; i++) {
        if (!home_air_values[i] || !home_air_names[i]) continue;
        if (i >= sensor_count) {
            set_static_ui_label(home_air_names[i], default_air_names[i]);
            lv_label_set_text(home_air_values[i], "---");
            lv_obj_set_style_text_color(home_air_values[i], COLOR_TEXT_DIM, 0);
            continue;
        }
        const bridge_sensor_t *sensor = &data->sensors[i];
        unbind_static_ui_label(home_air_names[i]);
        lv_label_set_text(home_air_names[i], air_sensor_label(sensor, i));
        if (!sensor_value_available(sensor->value)) {
            lv_label_set_text(home_air_values[i], "—");
            lv_obj_set_style_text_color(home_air_values[i], COLOR_TEXT_DIM, 0);
            continue;
        }
        char text[28];
        snprintf(text, sizeof(text), "%.*s %.*s", (int)sizeof(sensor->value), sensor->value,
                 (int)sizeof(sensor->unit), sensor->unit);
        lv_label_set_text(home_air_values[i], text);
        lv_color_t color = COLOR_TEXT;
        if (strstr(sensor->unit, "ppm")) {
            color = co2_color(sensor->value);
        }
        lv_obj_set_style_text_color(home_air_values[i], color, 0);
    }

    home_climate_known = false;
    home_climate_entity[0] = 0;
    if (data->climate_valid && data->climate_count > 0) {
        const bridge_climate_t *climate = &data->climates[0];
        strncpy(home_climate_entity, climate->entity_id, sizeof(home_climate_entity) - 1);
        home_climate_entity[sizeof(home_climate_entity) - 1] = '\0';
        home_climate_known = !strcmp(climate->mode, "off") || !strcmp(climate->mode, "cool") ||
            !strcmp(climate->mode, "heat") || !strcmp(climate->mode, "dry") || !strcmp(climate->mode, "fan_only") ||
            !strcmp(climate->mode, "auto") || !strcmp(climate->mode, "heat_cool");
        home_climate_on = home_climate_known && strcmp(climate->mode, "off") != 0;
        if (climate->has_target_temp) { home_temp_confirmed = climate->target_temp; if (!home_temp_proposed && !feedback_blocks(home_action_find(home_climate_entity))) home_climate_target_value = climate->target_temp; }

        if (home_climate_name) {
            if (climate->name[0]) {
                unbind_static_ui_label(home_climate_name);
                lv_label_set_text(home_climate_name, climate->name);
            } else set_static_ui_label(home_climate_name, "Кондиционер");
        }
        if (home_climate_current) {
            char text[48];
            unbind_static_ui_label(home_climate_current);
            if (climate->has_current_temp) snprintf(text, sizeof(text), ui_text("Сейчас %.1f C"), climate->current_temp);
            else snprintf(text, sizeof(text), "%s", ui_text("Сейчас нет данных"));
            lv_label_set_text(home_climate_current, text);
        }
        if (home_climate_target) {
            char text[48];
            unbind_static_ui_label(home_climate_target);
            if (climate->has_target_temp) snprintf(text, sizeof(text), ui_text("Цель %.0f C"), home_temp_confirmed);
            else snprintf(text, sizeof(text), "%s", ui_text("Цель нет данных"));
            lv_label_set_text(home_climate_target, text);
        }
        if (home_climate_power) {
            lv_obj_set_style_bg_color(home_climate_power, home_climate_on ? COLOR_LIGHT_ON : COLOR_LIGHT_OFF, 0);
        }
        static const char *modes[] = {"cool", "heat", "dry", "fan_only"};
        for (int i = 0; i < 4; i++) {
            if (!home_climate_mode_btns[i]) continue;
            lv_obj_set_style_bg_color(home_climate_mode_btns[i],
                strcmp(climate->mode, modes[i]) == 0 ? COLOR_ACCENT : COLOR_LIGHT_OFF, 0);
        }
    }

    // Old room sensor controls remain intentionally uncreated: the named air
    // overview above is the single source for household air detail.
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
            {"sensor.zhimi_vb4_f663_temperature",       ui_text("Темп.(очист.)")},
            {"sensor.zhimi_vb4_f663_relative_humidity",  ui_text("Влажн.(очист.)")},
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
                        if (!sensor_value_available(sen->value)) break;

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
                            lv_obj_set_style_text_color(room_sensor_val_lbl[r][s], co2_color(sen->value), 0);
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

bool ui_dashboard_reader_open(void) { return reader_overlay != NULL; }

void ui_dashboard_apply_preferences(const web_preferences_t *prefs)
{
    if (!prefs || !web_preferences_valid(prefs) ||
        ((prefs->backlight_percent || prefs->night_backlight_percent || prefs->night_dim) && !brightness_runtime_ready())) return;
    ui_dashboard_hide_sensitive();
    ui_strings_set_locale(prefs->locale);
    ui_preferences = *prefs;
    ui_has_preferences = true;
    refresh_static_ui_labels();
    set_calendar_day_names();
    if (lbl_topbar_temp) {
        char city[96];
        snprintf(city, sizeof(city), "%s · %s", prefs->name, prefs->city);
        lv_label_set_text(lbl_topbar_temp, city);
    }
    refresh_settings();
    if (page1_schedule_panel) {
        if (prefs->visible_calendar) lv_obj_clear_flag(page1_schedule_panel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page1_schedule_panel, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *day_cards[] = {day_weather_card, day_transport_card, day_air_card};
    const bool visible[] = {prefs->visible_weather, prefs->visible_transport, prefs->visible_home};
    for (int i = 0; i < 3; ++i) if (day_cards[i]) {
        if (visible[i]) lv_obj_clear_flag(day_cards[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(day_cards[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (page1_overview_panel) {
        bool show = prefs->visible_weather || prefs->visible_transport || prefs->visible_home;
        if (show) lv_obj_clear_flag(page1_overview_panel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page1_overview_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (lbl_topbar_temp && lbl_weather_detail) {
        if (weather_canvas) {
            if (prefs->visible_weather) lv_obj_clear_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);
        }
        if (prefs->visible_weather) {
            lv_obj_clear_flag(lbl_topbar_temp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_weather_detail, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lbl_topbar_temp, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_weather_detail, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (lbl_transport_out) {
        if (prefs->visible_transport) {
            lv_obj_clear_flag(lbl_transport_out, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(lbl_transport_in, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(lbl_transport_out, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(lbl_transport_in, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (page2_health_panel) {
        if (prefs->visible_health) lv_obj_clear_flag(page2_health_panel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page2_health_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (page2_tasks_panel) {
        if (prefs->visible_tasks) lv_obj_clear_flag(page2_tasks_panel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page2_tasks_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (page2_news_panel) {
        if (prefs->visible_news) lv_obj_clear_flag(page2_news_panel, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page2_news_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (page3_root) {
        if (prefs->visible_home) lv_obj_clear_flag(page3_root, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(page3_root, LV_OBJ_FLAG_HIDDEN);
    }
    refresh_week_strip();
    if (sel_year > 0) {
        update_schedule_title(sel_year, sel_month, sel_day);
        update_day_context(sel_year, sel_month, sel_day, last_cal_event_count);
    }
    ui_dashboard_update_bridge(&ui_bridge_snapshot);
    ui_dashboard_update_transport(&ui_bridge_snapshot.transport);
    ui_dashboard_update_weather(&ui_bridge_snapshot.weather);
    if (ui_calendar_snapshot.valid) ui_dashboard_update_ha_calendar(&ui_calendar_snapshot);
    ui_dashboard_update_ha(&ui_bridge_snapshot);
    ui_dashboard_update_time();
    refresh_reader_locale();
}

void ui_dashboard_reset_preferences(void)
{
    ui_dashboard_hide_sensitive();
    ui_strings_set_locale("ru");
    refresh_static_ui_labels();
    ui_has_preferences = false;
    memset(&ui_preferences, 0, sizeof(ui_preferences));
    ui_last_touch = esp_timer_get_time() / 1000000;
    wake_override_until = 0;
    if (lbl_topbar_temp) lv_label_set_text(lbl_topbar_temp, WEATHER_CITY);
    if (weather_canvas) lv_obj_clear_flag(weather_canvas, LV_OBJ_FLAG_HIDDEN);
    for (lv_obj_t *card : {day_weather_card, day_transport_card, day_air_card}) if (card) lv_obj_clear_flag(card, LV_OBJ_FLAG_HIDDEN);
    refresh_settings();
    if (page1_schedule_panel) lv_obj_clear_flag(page1_schedule_panel, LV_OBJ_FLAG_HIDDEN);
    if (page1_overview_panel) lv_obj_clear_flag(page1_overview_panel, LV_OBJ_FLAG_HIDDEN);
    if (lbl_topbar_temp) lv_obj_clear_flag(lbl_topbar_temp, LV_OBJ_FLAG_HIDDEN);
    if (lbl_weather_detail) lv_obj_clear_flag(lbl_weather_detail, LV_OBJ_FLAG_HIDDEN);
    if (lbl_transport_out) {
        lv_obj_clear_flag(lbl_transport_out, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl_transport_in, LV_OBJ_FLAG_HIDDEN);
    }
    if (page2_health_panel) lv_obj_clear_flag(page2_health_panel, LV_OBJ_FLAG_HIDDEN);
    if (page2_tasks_panel) lv_obj_clear_flag(page2_tasks_panel, LV_OBJ_FLAG_HIDDEN);
    if (page2_news_panel) lv_obj_clear_flag(page2_news_panel, LV_OBJ_FLAG_HIDDEN);
    if (page3_root) lv_obj_clear_flag(page3_root, LV_OBJ_FLAG_HIDDEN);
    refresh_static_ui_labels();
    set_calendar_day_names();
    refresh_week_strip();
    set_screen_off(false);
    ui_dashboard_update_bridge(&ui_bridge_snapshot);
    ui_dashboard_update_transport(&ui_bridge_snapshot.transport);
    ui_dashboard_update_weather(&ui_bridge_snapshot.weather);
    if (ui_calendar_snapshot.valid) ui_dashboard_update_ha_calendar(&ui_calendar_snapshot);
    ui_dashboard_update_ha(&ui_bridge_snapshot);
    ui_dashboard_update_time();
    refresh_reader_locale();
}

#if defined(SIMULATOR)
void ui_dashboard_sim_refresh_feedback(void) { home_feedback_tick(NULL); }
void ui_dashboard_sim_set_selected_temperature(float value) { home_climate_target_value = value; }
void ui_dashboard_sim_set_page(int page)
{
    if (tileview && page >= 0 && page < PAGE_COUNT) lv_tileview_set_tile(tileview, nav_tiles[page], LV_ANIM_OFF);
}

int ui_dashboard_sim_current_page(void)
{
    if (!tileview) return -1;
    lv_obj_t *active = lv_tileview_get_tile_active(tileview);
    for (int i = 0; i < PAGE_COUNT; ++i) if (nav_tiles[i] == active) return i;
    return -1;
}

bool ui_dashboard_sim_fixed_labels_fit(void)
{
    static const char *keys[] = {"День", "Сводка", "Дом", "Расписание", "Здоровье",
        "Задачи", "Новости", "Фоторамка", "Обновить", "Воздух дома", "Календарь",
        "Настройки HomeDash: подключитесь к Bridge", "Настройки HomeDash: откройте Bridge",
        "Авто: через 5 мин без касаний", "Вкл / выкл", "Охл.", "Тпл.", "Суш.", "Вен.",
        "Настройки", "Веб-панель Bridge", "Локальное восстановление",
        "Показать пароль", "Скрыть пароль", "Включить на 10 минут", "Выключить сеть",
        "Проверить обновление", "Охлаждение", "Обогрев", "Осушение", "Вентиляция"};
    for (const auto &binding : static_ui_labels) {
        if (!binding.label || !binding.key) continue;
        bool check = false;
        for (const char *key : keys) if (!strcmp(binding.key, key)) { check = true; break; }
        if (!check) continue;
        const lv_font_t *font = lv_obj_get_style_text_font(binding.label, LV_PART_MAIN);
        const char *text = ui_text(binding.key);
        if (!font || !text) return false;
        lv_point_t measured = {};
        lv_text_get_size(&measured, text, font,
                         lv_obj_get_style_text_letter_space(binding.label, LV_PART_MAIN),
                         lv_obj_get_style_text_line_space(binding.label, LV_PART_MAIN),
                         LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (measured.x > lv_obj_get_width(binding.label)) {
            std::fprintf(stderr, "[SIM] Fixed label overflows width: %s (%d > %d)\n",
                         text, measured.x, lv_obj_get_width(binding.label));
            return false;
        }
    }
    return true;
}
#endif

void ui_dashboard_update_recovery(const char *ssid, const char *password, const char *url)
{
    ui_dashboard_hide_sensitive();
    ui_recovery_active = ssid && ssid[0];
    snprintf(ui_recovery_ssid, sizeof(ui_recovery_ssid), "%s", ui_recovery_active ? ssid : "");
    snprintf(ui_recovery_password, sizeof(ui_recovery_password), "%s", ui_recovery_active && password ? password : "");
    snprintf(ui_recovery_url, sizeof(ui_recovery_url), "%s", ui_recovery_active && url ? url : "");
    refresh_settings();
}

bool ui_dashboard_filter_wake_touch(bool pressed, lv_indev_data_t *data)
{
    int64_t now_seconds = esp_timer_get_time() / 1000000;
    if (wake_swallow_until_release) {
        if (!pressed) wake_swallow_until_release = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return true;
    }
    if (pressed && ui_screen_off) {
        wake_swallow_until_release = true;
        ui_last_touch = now_seconds;
        wake_override_until = now_seconds + 60;
        if (ui_has_preferences) ui_dashboard_apply_brightness(&ui_preferences);
        set_screen_off(false);
        data->state = LV_INDEV_STATE_RELEASED;
        return true;
    }
    if (pressed) {
        ui_last_touch = now_seconds;
        if (ui_has_preferences && ui_preferences.night_dim && brightness_runtime_ready()) {
            time_t now; time(&now); struct tm local = {}; localtime_r(&now, &local);
            if (clock_is_night_for(ui_preferences, local)) {
                wake_override_until = now_seconds + 60;
                ui_dashboard_apply_brightness(&ui_preferences);
            }
        }
    }
    return false;
}

bool ui_dashboard_timezone_matches(const char *timezone, const char *posix_timezone) {
    return timezone && posix_timezone && ui_has_preferences && !strcmp(timezone, ui_preferences.timezone) && !strcmp(posix_timezone, ui_preferences.posix_timezone);
}

void ui_dashboard_photo_exit(void) {
    ui_last_touch = esp_timer_get_time() / 1000000;
}
