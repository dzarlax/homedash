#include "bridge.h"
#include "config.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "bridge";

// Copy up to dst_size-1 bytes from src, but never split a multi-byte UTF-8 char.
static void utf8_strncpy(char *dst, const char *src, size_t dst_size)
{
    if (!dst || !src || dst_size == 0) return;
    size_t max = dst_size - 1;
    size_t i = 0;
    while (i < max && src[i]) {
        unsigned char c = (unsigned char)src[i];
        int seq_len = 1;
        if (c >= 0xF0)      seq_len = 4;
        else if (c >= 0xE0) seq_len = 3;
        else if (c >= 0xC0) seq_len = 2;
        if (i + seq_len > max) break;
        i += seq_len;
    }
    memcpy(dst, src, i);
    dst[i] = '\0';
}

static bridge_data_t s_data = {};
static bridge_cal_data_t s_cal_data = {};
static char s_last_error[64] = "Waiting...";

typedef struct {
    char *data;
    int   len;
    int   cap;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_DATA:
        if (buf->len + evt->data_len >= buf->cap) {
            int new_cap = buf->cap + evt->data_len + 1024;
            char *tmp = (char *)realloc(buf->data, new_cap);
            if (!tmp) {
                ESP_LOGE(TAG, "OOM growing response buffer");
                return ESP_FAIL;
            }
            buf->data = tmp;
            buf->cap = new_cap;
        }
        memcpy(buf->data + buf->len, evt->data, evt->data_len);
        buf->len += evt->data_len;
        buf->data[buf->len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

// Allocate and perform HTTP GET, return response buffer. Caller must free buf->data.
static bool http_get(const char *url, http_buf_t *buf)
{
    buf->cap = 4096;
    buf->len = 0;
    buf->data = (char *)malloc(buf->cap);
    if (!buf->data) return false;

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.user_data = buf;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        free(buf->data);
        buf->data = NULL;
        return false;
    }
    return true;
}

static void parse_health(cJSON *obj)
{
    if (!obj || !cJSON_IsObject(obj)) {
        s_data.health.valid = false;
        return;
    }
    s_data.health.steps      = cJSON_GetObjectItem(obj, "steps")      ? cJSON_GetObjectItem(obj, "steps")->valueint : 0;
    s_data.health.steps_prev = cJSON_GetObjectItem(obj, "steps_prev") ? cJSON_GetObjectItem(obj, "steps_prev")->valueint : 0;
    s_data.health.cal        = cJSON_GetObjectItem(obj, "cal")        ? cJSON_GetObjectItem(obj, "cal")->valueint : 0;
    s_data.health.cal_prev   = cJSON_GetObjectItem(obj, "cal_prev")   ? cJSON_GetObjectItem(obj, "cal_prev")->valueint : 0;
    s_data.health.sleep      = cJSON_GetObjectItem(obj, "sleep")      ? (float)cJSON_GetObjectItem(obj, "sleep")->valuedouble : 0;
    s_data.health.sleep_prev = cJSON_GetObjectItem(obj, "sleep_prev") ? (float)cJSON_GetObjectItem(obj, "sleep_prev")->valuedouble : 0;
    s_data.health.hr         = cJSON_GetObjectItem(obj, "hr")         ? cJSON_GetObjectItem(obj, "hr")->valueint : 0;
    s_data.health.rhr        = cJSON_GetObjectItem(obj, "rhr")        ? cJSON_GetObjectItem(obj, "rhr")->valueint : 0;
    s_data.health.hrv        = cJSON_GetObjectItem(obj, "hrv")        ? cJSON_GetObjectItem(obj, "hrv")->valueint : 0;
    s_data.health.spo2       = cJSON_GetObjectItem(obj, "spo2")       ? cJSON_GetObjectItem(obj, "spo2")->valueint : 0;
    s_data.health.readiness  = cJSON_GetObjectItem(obj, "readiness")  ? cJSON_GetObjectItem(obj, "readiness")->valueint : 0;
    s_data.health.valid = true;
}

static void parse_tasks(cJSON *arr)
{
    s_data.task_count = 0;
    s_data.tasks_valid = false;
    if (!arr || !cJSON_IsArray(arr)) return;

    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_MAX_TASKS) n = BRIDGE_MAX_TASKS;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        bridge_task_t *t = &s_data.tasks[s_data.task_count];
        memset(t, 0, sizeof(*t));

        cJSON *title = cJSON_GetObjectItem(item, "t");
        if (title && cJSON_IsString(title)) {
            utf8_strncpy(t->title, title->valuestring, sizeof(t->title));
        }
        cJSON *prio = cJSON_GetObjectItem(item, "p");
        if (prio) t->priority = prio->valueint;

        cJSON *due = cJSON_GetObjectItem(item, "due");
        if (due && cJSON_IsString(due)) {
            strncpy(t->due, due->valuestring, sizeof(t->due) - 1);
        }
        s_data.task_count++;
    }
    s_data.tasks_valid = true;
}

static void parse_news(cJSON *arr)
{
    s_data.news_count = 0;
    s_data.news_valid = false;
    if (!arr || !cJSON_IsArray(arr)) return;

    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_MAX_NEWS) n = BRIDGE_MAX_NEWS;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        bridge_news_t *nw = &s_data.news[s_data.news_count];
        memset(nw, 0, sizeof(*nw));

        cJSON *title = cJSON_GetObjectItem(item, "t");
        if (title && cJSON_IsString(title)) {
            utf8_strncpy(nw->title, title->valuestring, sizeof(nw->title));
        }
        cJSON *cat = cJSON_GetObjectItem(item, "c");
        if (cat && cJSON_IsString(cat)) {
            strncpy(nw->category, cat->valuestring, sizeof(nw->category) - 1);
        }
        cJSON *h = cJSON_GetObjectItem(item, "h");
        if (h) nw->hours_ago = h->valueint;

        s_data.news_count++;
    }
    s_data.news_valid = true;
}

static void parse_sensors(cJSON *arr)
{
    s_data.sensor_count = 0;
    s_data.sensors_valid = false;
    if (!arr || !cJSON_IsArray(arr)) return;

    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_MAX_SENSORS) n = BRIDGE_MAX_SENSORS;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        bridge_sensor_t *s = &s_data.sensors[s_data.sensor_count];
        memset(s, 0, sizeof(*s));

        cJSON *name = cJSON_GetObjectItem(item, "n");
        if (name && cJSON_IsString(name)) {
            strncpy(s->name, name->valuestring, sizeof(s->name) - 1);
        }
        cJSON *val = cJSON_GetObjectItem(item, "v");
        if (val && cJSON_IsString(val)) {
            strncpy(s->value, val->valuestring, sizeof(s->value) - 1);
        }
        cJSON *unit = cJSON_GetObjectItem(item, "u");
        if (unit && cJSON_IsString(unit)) {
            strncpy(s->unit, unit->valuestring, sizeof(s->unit) - 1);
        }
        s_data.sensor_count++;
    }
    s_data.sensors_valid = true;
}

static void parse_lights(cJSON *arr)
{
    s_data.light_count = 0;
    s_data.lights_valid = false;
    if (!arr || !cJSON_IsArray(arr)) return;

    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_MAX_LIGHTS) n = BRIDGE_MAX_LIGHTS;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;

        bridge_light_t *l = &s_data.lights[s_data.light_count];
        memset(l, 0, sizeof(*l));

        cJSON *id = cJSON_GetObjectItem(item, "id");
        if (id && cJSON_IsString(id)) {
            strncpy(l->entity_id, id->valuestring, sizeof(l->entity_id) - 1);
        }
        cJSON *name = cJSON_GetObjectItem(item, "n");
        if (name && cJSON_IsString(name)) {
            strncpy(l->name, name->valuestring, sizeof(l->name) - 1);
        }
        cJSON *on = cJSON_GetObjectItem(item, "on");
        if (on) l->on = cJSON_IsTrue(on);

        cJSON *br = cJSON_GetObjectItem(item, "br");
        if (br) l->brightness = br->valueint;

        s_data.light_count++;
    }
    s_data.lights_valid = true;
}

static void parse_weather(cJSON *obj)
{
    s_data.weather.valid = false;
    if (!obj || !cJSON_IsObject(obj)) return;

    cJSON *v;
    v = cJSON_GetObjectItem(obj, "temp"); if (v) s_data.weather.temp = (float)v->valuedouble;
    v = cJSON_GetObjectItem(obj, "hum");  if (v) s_data.weather.humidity = (float)v->valuedouble;
    v = cJSON_GetObjectItem(obj, "wind"); if (v) s_data.weather.wind_speed = (float)v->valuedouble;
    v = cJSON_GetObjectItem(obj, "wc");   if (v) s_data.weather.weather_code = v->valueint;

    s_data.weather.daily_count = 0;
    cJSON *daily = cJSON_GetObjectItem(obj, "daily");
    if (daily && cJSON_IsArray(daily)) {
        int n = cJSON_GetArraySize(daily);
        if (n > BRIDGE_FORECAST_DAYS) n = BRIDGE_FORECAST_DAYS;
        for (int i = 0; i < n; i++) {
            cJSON *d = cJSON_GetArrayItem(daily, i);
            if (!d) continue;
            bridge_weather_daily_t *wd = &s_data.weather.daily[i];
            v = cJSON_GetObjectItem(d, "tmax"); if (v) wd->temp_max = (float)v->valuedouble;
            v = cJSON_GetObjectItem(d, "tmin"); if (v) wd->temp_min = (float)v->valuedouble;
            v = cJSON_GetObjectItem(d, "wc");   if (v) wd->weather_code = v->valueint;
            s_data.weather.daily_count++;
        }
    }
    s_data.weather.valid = true;
}

static void parse_transport(cJSON *arr)
{
    s_data.transport.valid = false;
    if (!arr || !cJSON_IsArray(arr)) return;

    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_TRANSPORT_STOPS) n = BRIDGE_TRANSPORT_STOPS;

    for (int i = 0; i < n; i++) {
        cJSON *stop = cJSON_GetArrayItem(arr, i);
        if (!stop) continue;

        bridge_transport_stop_t *ts = &s_data.transport.stops[i];
        ts->count = 0;

        cJSON *vehicles = cJSON_GetObjectItem(stop, "vehicles");
        if (!vehicles || !cJSON_IsArray(vehicles)) continue;

        int vc = cJSON_GetArraySize(vehicles);
        if (vc > BRIDGE_TRANSPORT_VEHICLES) vc = BRIDGE_TRANSPORT_VEHICLES;

        for (int j = 0; j < vc; j++) {
            cJSON *v = cJSON_GetArrayItem(vehicles, j);
            if (!v) continue;
            bridge_transport_vehicle_t *tv = &ts->vehicles[ts->count];
            memset(tv, 0, sizeof(*tv));

            cJSON *ln = cJSON_GetObjectItem(v, "ln");
            if (ln && cJSON_IsString(ln)) strncpy(tv->line_number, ln->valuestring, sizeof(tv->line_number) - 1);
            cJSON *sl = cJSON_GetObjectItem(v, "sl");
            if (sl) tv->seconds_left = sl->valueint;
            cJSON *sb = cJSON_GetObjectItem(v, "sb");
            if (sb) tv->stations_between = sb->valueint;
            ts->count++;
        }
    }
    s_data.transport.valid = true;
}

void bridge_fetch_and_update(void)
{
    if (!wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "No WiFi");
        return;
    }

    snprintf(s_last_error, sizeof(s_last_error), "Fetching...");

    char url[256];
    snprintf(url, sizeof(url), "%s/api/dashboard?key=%s", BRIDGE_URL, BRIDGE_API_KEY);

    http_buf_t resp = {};
    if (!http_get(url, &resp)) {
        snprintf(s_last_error, sizeof(s_last_error), "HTTP error");
        return;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);
    if (!root) {
        snprintf(s_last_error, sizeof(s_last_error), "JSON parse error");
        return;
    }

    cJSON *ts = cJSON_GetObjectItem(root, "ts");
    if (ts) s_data.ts = (uint32_t)ts->valueint;

    parse_health(cJSON_GetObjectItem(root, "health"));
    parse_tasks(cJSON_GetObjectItem(root, "tasks"));
    parse_news(cJSON_GetObjectItem(root, "news"));
    parse_sensors(cJSON_GetObjectItem(root, "sensors"));
    parse_lights(cJSON_GetObjectItem(root, "lights"));
    parse_weather(cJSON_GetObjectItem(root, "weather"));
    parse_transport(cJSON_GetObjectItem(root, "transport"));

    cJSON_Delete(root);
    snprintf(s_last_error, sizeof(s_last_error), "OK");
}

const bridge_data_t *bridge_get_data(void)
{
    return &s_data;
}

const bridge_cal_data_t *bridge_get_calendar_data(void)
{
    return &s_cal_data;
}

const char *bridge_get_last_error(void)
{
    return s_last_error;
}

void bridge_toggle_light(const char *entity_id)
{
    if (!wifi_is_connected() || !entity_id) return;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/ha/action?key=%s", BRIDGE_URL, BRIDGE_API_KEY);

    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"action\":\"toggle\"}", entity_id);

    http_buf_t resp = {};
    resp.cap = 2048;
    resp.len = 0;
    resp.data = (char *)malloc(resp.cap);
    if (!resp.data) return;

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.event_handler = http_event_handler;
    config.user_data = &resp;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200 && resp.data) {
        ESP_LOGI(TAG, "Toggle %s OK", entity_id);
        // Parse fresh lights from response
        cJSON *root = cJSON_Parse(resp.data);
        if (root) {
            cJSON *lights = cJSON_GetObjectItem(root, "lights");
            if (lights) {
                parse_lights(lights);
            }
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGE(TAG, "Toggle %s failed: HTTP %d", entity_id, status);
    }
    free(resp.data);
}

void bridge_fetch_calendar(int year, int month, int day)
{
    if (!wifi_is_connected()) return;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/calendar?date=%04d-%02d-%02d&key=%s",
             BRIDGE_URL, year, month, day, BRIDGE_API_KEY);

    http_buf_t resp = {};
    if (!http_get(url, &resp)) {
        ESP_LOGE(TAG, "Calendar fetch failed");
        return;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        return;
    }

    s_cal_data.count = 0;
    s_cal_data.year = year;
    s_cal_data.month = month;
    s_cal_data.day = day;

    int n = cJSON_GetArraySize(root);
    if (n > BRIDGE_CAL_MAX_EVENTS) n = BRIDGE_CAL_MAX_EVENTS;

    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!item) continue;

        bridge_cal_event_t *ev = &s_cal_data.events[s_cal_data.count];
        memset(ev, 0, sizeof(*ev));

        cJSON *s = cJSON_GetObjectItem(item, "s");
        if (s && cJSON_IsString(s)) {
            utf8_strncpy(ev->summary, s->valuestring, sizeof(ev->summary));
        }
        cJSON *sh = cJSON_GetObjectItem(item, "sh"); if (sh) ev->start_hour = sh->valueint;
        cJSON *sm = cJSON_GetObjectItem(item, "sm"); if (sm) ev->start_min = sm->valueint;
        cJSON *eh = cJSON_GetObjectItem(item, "eh"); if (eh) ev->end_hour = eh->valueint;
        cJSON *em = cJSON_GetObjectItem(item, "em"); if (em) ev->end_min = em->valueint;
        cJSON *ad = cJSON_GetObjectItem(item, "ad"); if (ad) ev->all_day = cJSON_IsTrue(ad);
        cJSON *ci = cJSON_GetObjectItem(item, "ci"); if (ci) ev->cal_idx = ci->valueint;

        s_cal_data.count++;
    }
    s_cal_data.valid = true;
    cJSON_Delete(root);
}
