#include "bridge.h"
#include "wifi_manager.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <cmath>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "web_identity.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

static const char *TAG = "bridge";

static char s_bridge_url[128] = {};
static char s_bridge_key[64] = {};
static SemaphoreHandle_t s_data_mutex = NULL;

void bridge_init(const char *url, const char *api_key)
{
    strncpy(s_bridge_url, url, sizeof(s_bridge_url) - 1);
    strncpy(s_bridge_key, api_key, sizeof(s_bridge_key) - 1);
    if (!s_data_mutex) s_data_mutex = xSemaphoreCreateMutex();
}

const char *bridge_get_url(void) { return s_bridge_url; }
const char *bridge_get_api_key(void) { return s_bridge_key; }

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
    uint8_t calendar_state; // 0 unknown, 1 complete, 2 partial.
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *buf = (http_buf_t *)evt->user_data;
    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (evt->header_key && evt->header_value && !strcasecmp(evt->header_key, "X-HomeDash-Calendar-State")) {
            if (!strcmp(evt->header_value, "partial")) buf->calendar_state = 2;
            else if (!strcmp(evt->header_value, "complete") && buf->calendar_state != 2) buf->calendar_state = 1;
        }
        break;
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
// err_out: optional buffer to write error description (e.g. "ESP_OK/403")
static bool http_get(const char *url, http_buf_t *buf, char *err_out, size_t err_out_size)
{
    buf->cap = 4096;
    buf->len = 0;
    buf->data = (char *)malloc(buf->cap);
    if (!buf->data) {
        if (err_out) snprintf(err_out, err_out_size, "OOM");
        return false;
    }

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.user_data = buf;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "X-API-Key", s_bridge_key);
    if (web_identity_set_http_header(client) != ESP_OK) {
        esp_http_client_cleanup(client);
        free(buf->data);
        buf->data = NULL;
        return false;
    }
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "http_get failed: err=%s status=%d", esp_err_to_name(err), status);
        if (err_out) snprintf(err_out, err_out_size, "%s/%d", esp_err_to_name(err), status);
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
        cJSON *summary = cJSON_GetObjectItem(item, "s");
        if (summary && cJSON_IsString(summary)) {
            utf8_strncpy(nw->summary, summary->valuestring, sizeof(nw->summary));
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

static void parse_climate(cJSON *arr)
{
    s_data.climate_count = 0;
    s_data.climate_valid = false;
    if (!arr || !cJSON_IsArray(arr)) return;

    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_MAX_CLIMATES) n = BRIDGE_MAX_CLIMATES;
    for (int i = 0; i < n; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        if (!item) continue;
        bridge_climate_t *climate = &s_data.climates[s_data.climate_count];
        memset(climate, 0, sizeof(*climate));

        cJSON *id = cJSON_GetObjectItem(item, "id");
        if (!id || !cJSON_IsString(id)) continue;
        strncpy(climate->entity_id, id->valuestring, sizeof(climate->entity_id) - 1);
        cJSON *name = cJSON_GetObjectItem(item, "n");
        if (name && cJSON_IsString(name)) utf8_strncpy(climate->name, name->valuestring, sizeof(climate->name));
        cJSON *mode = cJSON_GetObjectItem(item, "mode");
        if (mode && cJSON_IsString(mode)) strncpy(climate->mode, mode->valuestring, sizeof(climate->mode) - 1);
        cJSON *current = cJSON_GetObjectItem(item, "cur");
        if (current && cJSON_IsNumber(current)) {
            climate->current_temp = (float)current->valuedouble;
            climate->has_current_temp = true;
        }
        cJSON *target = cJSON_GetObjectItem(item, "target");
        if (target && cJSON_IsNumber(target)) {
            climate->target_temp = (float)target->valuedouble;
            climate->has_target_temp = true;
        }
        s_data.climate_count++;
    }
    s_data.climate_valid = true;
}

static bool finite_number(cJSON *v) {
    return cJSON_IsNumber(v) && std::isfinite(v->valuedouble);
}

static void parse_weather(cJSON *obj)
{
    // Preserve the last good observation when a section is absent or malformed.
    s_data.weather.last_attempt_failed = true;
    if (!cJSON_IsObject(obj)) return;
    cJSON *temp = cJSON_GetObjectItem(obj, "temp");
    cJSON *code = cJSON_GetObjectItem(obj, "wc");
    if (!finite_number(temp) || !finite_number(code)) return;
    bridge_weather_t fresh = {};
    fresh.temp = temp->valuedouble;
    fresh.weather_code = code->valueint;
    cJSON *v = cJSON_GetObjectItem(obj, "hum");
    if (finite_number(v)) fresh.humidity = v->valuedouble;
    v = cJSON_GetObjectItem(obj, "wind");
    if (finite_number(v)) fresh.wind_speed = v->valuedouble;
    cJSON *daily = cJSON_GetObjectItem(obj, "daily");
    if (cJSON_IsArray(daily)) {
        int n = cJSON_GetArraySize(daily);
        if (n > BRIDGE_FORECAST_DAYS) n = BRIDGE_FORECAST_DAYS;
        for (int i = 0; i < n; ++i) {
            cJSON *d = cJSON_GetArrayItem(daily, i);
            cJSON *hi = cJSON_GetObjectItem(d, "tmax"), *lo = cJSON_GetObjectItem(d, "tmin");
            cJSON *wc = cJSON_GetObjectItem(d, "wc");
            if (!finite_number(hi) || !finite_number(lo) || !finite_number(wc)) break;
            fresh.daily[fresh.daily_count++] = {(float)hi->valuedouble, (float)lo->valuedouble, wc->valueint};
        }
    }
    fresh.valid = true;
    fresh.received_ms = esp_timer_get_time() / 1000;
    s_data.weather = fresh;
}

static void parse_transport(cJSON *arr)
{
    s_data.transport.last_attempt_failed = true;
    if (!cJSON_IsArray(arr)) return;
    bridge_transport_t fresh = {};
    int n = cJSON_GetArraySize(arr);
    if (n > BRIDGE_TRANSPORT_STOPS) n = BRIDGE_TRANSPORT_STOPS;
    for (int i = 0; i < n; ++i) {
        cJSON *stop = cJSON_GetArrayItem(arr, i);
        cJSON *vehicles = cJSON_GetObjectItem(stop, "vehicles");
        if (!cJSON_IsObject(stop) || !cJSON_IsArray(vehicles)) return;
        int vc = cJSON_GetArraySize(vehicles);
        if (vc > BRIDGE_TRANSPORT_VEHICLES) vc = BRIDGE_TRANSPORT_VEHICLES;
        for (int j = 0; j < vc; ++j) {
            cJSON *v = cJSON_GetArrayItem(vehicles, j);
            cJSON *ln = cJSON_GetObjectItem(v, "ln");
            cJSON *sl = cJSON_GetObjectItem(v, "sl"), *sb = cJSON_GetObjectItem(v, "sb");
            if (!cJSON_IsString(ln) || !finite_number(sl) || !finite_number(sb)) return;
            bridge_transport_vehicle_t *tv = &fresh.stops[i].vehicles[fresh.stops[i].count++];
            snprintf(tv->line_number, sizeof(tv->line_number), "%s", ln->valuestring);
            tv->seconds_left = sl->valueint;
            tv->stations_between = sb->valueint;
        }
    }
    fresh.valid = true;
    fresh.received_ms = esp_timer_get_time() / 1000;
    s_data.transport = fresh;
}

static void mark_dashboard_failure() {
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    s_data.weather.last_attempt_failed = s_data.transport.last_attempt_failed = true;
    xSemaphoreGive(s_data_mutex);
}

void bridge_fetch_and_update(void)
{
    if (!wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "No WiFi");
        mark_dashboard_failure();
        return;
    }

    snprintf(s_last_error, sizeof(s_last_error), "Fetching...");

    char url[256];
    snprintf(url, sizeof(url), "%s/api/dashboard", s_bridge_url);

    http_buf_t resp = {};
    if (!http_get(url, &resp, s_last_error, sizeof(s_last_error))) {
        mark_dashboard_failure();
        return;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);
    if (!cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        mark_dashboard_failure();
        snprintf(s_last_error, sizeof(s_last_error), "JSON parse error");
        return;
    }

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    cJSON *ts = cJSON_GetObjectItem(root, "ts");
    if (ts) s_data.ts = (uint32_t)ts->valueint;

    parse_health(cJSON_GetObjectItem(root, "health"));
    parse_tasks(cJSON_GetObjectItem(root, "tasks"));
    parse_news(cJSON_GetObjectItem(root, "news"));
    parse_sensors(cJSON_GetObjectItem(root, "sensors"));
    parse_lights(cJSON_GetObjectItem(root, "lights"));
    parse_climate(cJSON_GetObjectItem(root, "climate"));
    parse_weather(cJSON_GetObjectItem(root, "weather"));
    parse_transport(cJSON_GetObjectItem(root, "transport"));
    xSemaphoreGive(s_data_mutex);

    cJSON_Delete(root);
    snprintf(s_last_error, sizeof(s_last_error), "OK");
}

bool bridge_has_data(void)
{
    if (!s_data_mutex) return false;
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    bool available = s_data.ts > 0;
    xSemaphoreGive(s_data_mutex);
    return available;
}

bool bridge_copy_data(bridge_data_t *out)
{
    if (!out || !s_data_mutex) return false;
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_data_mutex);
    return true;
}

bool bridge_copy_calendar_data(bridge_cal_data_t *out)
{
    if (!out || !s_data_mutex) return false;
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    *out = s_cal_data;
    xSemaphoreGive(s_data_mutex);
    return true;
}

bool bridge_copy_last_error(char *out, size_t out_size)
{
    if (!out || out_size == 0 || !s_data_mutex) return false;
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    strncpy(out, s_last_error, out_size - 1);
    out[out_size - 1] = '\0';
    xSemaphoreGive(s_data_mutex);
    return true;
}

namespace {
constexpr size_t ACTION_RESPONSE_LIMIT = 8192;
struct action_response_t { char *data; size_t used; bool overflow; bool timed_out; int64_t deadline_us; };
static esp_err_t action_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    auto *response = static_cast<action_response_t *>(event->user_data);
    if (response && esp_timer_get_time() >= response->deadline_us) {
        response->timed_out = true;
        return ESP_FAIL;
    }
    if (!response || event->data_len < 0 || static_cast<size_t>(event->data_len) > ACTION_RESPONSE_LIMIT - response->used) {
        if (response) response->overflow = true;
        return ESP_FAIL;
    }
    memcpy(response->data + response->used, event->data, event->data_len);
    response->used += event->data_len;
    response->data[response->used] = 0;
    return ESP_OK;
}
static bool action_entity_valid(const char *entity)
{
    if (!entity || !*entity || strlen(entity) >= sizeof(action_observation_t::entity)) return false;
    if (strncmp(entity, "light.", 6) && strncmp(entity, "climate.", 8)) return false;
    for (const char *p = entity; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.')) return false;
    return true;
}
static bool action_mode_valid(const char *mode)
{
    return mode && (!strcmp(mode,"off") || !strcmp(mode,"cool") || !strcmp(mode,"heat") ||
           !strcmp(mode,"dry") || !strcmp(mode,"fan_only") || !strcmp(mode,"auto") || !strcmp(mode,"heat_cool"));
}
static action_error_t action_safe_error(const char *code, action_error_t fallback)
{
    if (!code) return fallback;
    if (!strcmp(code,"offline")) return ACTION_ERROR_OFFLINE;
    if (!strcmp(code,"queue_full")) return ACTION_ERROR_QUEUE_FULL;
    if (!strcmp(code,"unauthorized")) return ACTION_ERROR_UNAUTHORIZED;
    if (!strcmp(code,"unsupported")) return ACTION_ERROR_UNSUPPORTED;
    if (!strcmp(code,"upstream_unavailable")) return ACTION_ERROR_UPSTREAM_UNAVAILABLE;
    if (!strcmp(code,"invalid_response")) return ACTION_ERROR_INVALID_RESPONSE;
    if (!strcmp(code,"timeout")) return ACTION_ERROR_TIMEOUT;
    return fallback;
}
static bool parse_action_observation(cJSON *item, const char *expected, action_observation_t *out)
{
    if (!cJSON_IsObject(item)) return false;
    cJSON *entity = cJSON_GetObjectItemCaseSensitive(item,"entity_id");
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(item,"kind");
    cJSON *available = cJSON_GetObjectItemCaseSensitive(item,"available");
    cJSON *state = cJSON_GetObjectItemCaseSensitive(item,"state");
    if (!cJSON_IsString(entity) || !cJSON_IsString(kind) || !cJSON_IsString(state) || !cJSON_IsBool(available) ||
        strcmp(entity->valuestring,expected) || strlen(state->valuestring) >= sizeof(out->state)) return false;
    const bool light = !strncmp(expected,"light.",6);
    if (strcmp(kind->valuestring,light ? "light" : "climate")) return false;
    *out = {};
    snprintf(out->entity,sizeof(out->entity),"%s",expected);
    snprintf(out->kind,sizeof(out->kind),"%s",kind->valuestring);
    snprintf(out->state,sizeof(out->state),"%s",state->valuestring);
    out->available = cJSON_IsTrue(available);
    if (out->available && (light ? (strcmp(out->state,"on") && strcmp(out->state,"off")) : !action_mode_valid(out->state))) return false;
    cJSON *target = cJSON_GetObjectItemCaseSensitive(item,"target_temperature");
    if (target) {
        if (light || !cJSON_IsNumber(target) || !std::isfinite(target->valuedouble) || !std::isfinite(static_cast<float>(target->valuedouble))) return false;
        out->has_target = true; out->target = static_cast<float>(target->valuedouble);
    }
    return true;
}
static void merge_action_observation(const action_observation_t &observation)
{
    if (!observation.available || !s_data_mutex) return;
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    // A single-entity read must not replace the selected list or its names.
    if (!strcmp(observation.kind,"light")) {
        for (int i=0;i<s_data.light_count && i<BRIDGE_MAX_LIGHTS;++i)
            if (!strcmp(s_data.lights[i].entity_id,observation.entity))
                s_data.lights[i].on = !strcmp(observation.state,"on");
    } else {
        for (int i=0;i<s_data.climate_count && i<BRIDGE_MAX_CLIMATES;++i)
            if (!strcmp(s_data.climates[i].entity_id,observation.entity)) {
                snprintf(s_data.climates[i].mode,sizeof(s_data.climates[i].mode),"%s",observation.state);
                if (observation.has_target) {
                    s_data.climates[i].has_target_temp = true;
                    s_data.climates[i].target_temp = observation.target;
                }
            }
    }
    xSemaphoreGive(s_data_mutex);
}
static bridge_action_result_t action_http(const char *entity, const char *body, int timeout_ms)
{
    const bool post = body != nullptr;
    bridge_action_result_t result = {};
    result.phase = post ? ACTION_NOT_SENT : ACTION_EMPTY;
    result.error = ACTION_ERROR_UNSUPPORTED;
    if (!action_entity_valid(entity) || timeout_ms <= 0) return result;
    if (!wifi_is_connected()) { result.error = ACTION_ERROR_OFFLINE; return result; }
    char url[256];
    int length = post ? snprintf(url,sizeof(url),"%s/api/ha/action",s_bridge_url)
                      : snprintf(url,sizeof(url),"%s/api/ha/state?entity_id=%s",s_bridge_url,entity);
    if (!s_bridge_url[0] || length < 0 || length >= static_cast<int>(sizeof(url))) return result;
    action_response_t response = {};
    response.data = static_cast<char *>(malloc(ACTION_RESPONSE_LIMIT + 1));
    if (!response.data) { result.error = ACTION_ERROR_UPSTREAM_UNAVAILABLE; return result; }
    response.data[0] = 0;
    esp_http_client_config_t config = {};
    config.url = url; config.method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.timeout_ms = post ? 10000 : (timeout_ms < 3000 ? timeout_ms : 3000);
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.event_handler = action_http_event; config.user_data = &response;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(response.data); result.error = ACTION_ERROR_UPSTREAM_UNAVAILABLE; return result; }
    bool headers_ok = esp_http_client_set_header(client,"X-API-Key",s_bridge_key) == ESP_OK &&
        esp_http_client_set_header(client,"Accept-Encoding","identity") == ESP_OK &&
        web_identity_set_http_header(client) == ESP_OK;
    if (post) headers_ok = headers_ok && esp_http_client_set_header(client,"Content-Type","application/json") == ESP_OK &&
        esp_http_client_set_post_field(client,body,strlen(body)) == ESP_OK;
    if (!headers_ok) {
        esp_http_client_cleanup(client); free(response.data); result.error = ACTION_ERROR_UNAUTHORIZED; return result;
    }
    response.deadline_us = esp_timer_get_time() + static_cast<int64_t>(config.timeout_ms) * 1000;
    esp_err_t error = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    result.phase = post ? ACTION_UNKNOWN : ACTION_EMPTY;
    result.error = ACTION_ERROR_UPSTREAM_UNAVAILABLE;
    if (response.timed_out || esp_timer_get_time() >= response.deadline_us) {
        result.error = ACTION_ERROR_TIMEOUT; free(response.data); return result;
    }
    if (error != ESP_OK) {
        if (response.overflow) result.error = ACTION_ERROR_INVALID_RESPONSE;
#ifdef ESP_ERR_TIMEOUT
        if (error == ESP_ERR_TIMEOUT) result.error = ACTION_ERROR_TIMEOUT;
#endif
        free(response.data); return result;
    }
    if (status == 401 || status == 403) result.error = ACTION_ERROR_UNAUTHORIZED;
    else if (status == 404) result.error = ACTION_ERROR_UNSUPPORTED;
    else if (status == 408 || status == 504) result.error = ACTION_ERROR_TIMEOUT;
    if (post && status >= 400 && status < 500 && status != 408) result.phase = ACTION_REJECTED;
    if (response.overflow) { result.error = ACTION_ERROR_INVALID_RESPONSE; free(response.data); return result; }
    cJSON *root = cJSON_ParseWithLengthOpts(response.data,response.used + 1,nullptr,true);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root); free(response.data);
        if (status == 200) result.error = ACTION_ERROR_INVALID_RESPONSE;
        return result;
    }
    cJSON *remote_result = cJSON_GetObjectItemCaseSensitive(root,"action_result");
    cJSON *remote_error = cJSON_GetObjectItemCaseSensitive(remote_result,"error");
    if (!cJSON_IsString(remote_error)) remote_error = cJSON_GetObjectItemCaseSensitive(root,"error");
    if (cJSON_IsString(remote_error)) result.error = action_safe_error(remote_error->valuestring,result.error);
    if (status == 200) {
        if (post) {
            cJSON *state = cJSON_GetObjectItemCaseSensitive(remote_result,"state");
            cJSON *ok = cJSON_GetObjectItemCaseSensitive(root,"ok");
            if ((cJSON_IsString(state) && !strcmp(state->valuestring,"accepted")) ||
                (!remote_result && cJSON_IsTrue(ok))) {
                result.phase = ACTION_ACCEPTED; result.error = ACTION_ERROR_NONE;
            } else { result.error = ACTION_ERROR_INVALID_RESPONSE; }
        } else { result.error = ACTION_ERROR_NONE; }
        cJSON *observation = cJSON_GetObjectItemCaseSensitive(root,"observation");
        // Legacy aggregate snapshots cannot prove availability or causality.
        if (observation && parse_action_observation(observation,entity,&result.observation)) {
            result.has_observation = true;
        } else if (observation || !post) result.error = ACTION_ERROR_INVALID_RESPONSE;
    }
    cJSON_Delete(root); free(response.data); return result;
}
} // namespace

bridge_action_result_t bridge_action_request(const char *entity, const char *action, float temperature, const char *mode)
{
    bridge_action_result_t invalid = {}; invalid.phase = ACTION_NOT_SENT; invalid.error = ACTION_ERROR_UNSUPPORTED;
    if (!action_entity_valid(entity) || !action) return invalid;
    const bool climate = !strncmp(entity,"climate.",8);
    if (strcmp(action,"turn_on") && strcmp(action,"turn_off") &&
        (!climate || (strcmp(action,"set_temperature") && strcmp(action,"set_hvac_mode")))) return invalid;
    if (!strcmp(action,"set_temperature") && (!std::isfinite(temperature) || temperature < 16 || temperature > 30)) return invalid;
    if (!strcmp(action,"set_hvac_mode") && (!action_mode_valid(mode) || !strcmp(mode,"off"))) return invalid;
    cJSON *root = cJSON_CreateObject();
    if (!root) return invalid;
    cJSON_AddStringToObject(root,"entity_id",entity); cJSON_AddStringToObject(root,"action",action);
    if (!strcmp(action,"set_temperature")) cJSON_AddNumberToObject(root,"temperature",temperature);
    if (!strcmp(action,"set_hvac_mode")) cJSON_AddStringToObject(root,"hvac_mode",mode);
    char *body = cJSON_PrintUnformatted(root); cJSON_Delete(root);
    if (!body) return invalid;
    auto result = action_http(entity,body,10000); free(body); return result;
}
void bridge_action_merge_observation(const action_observation_t &observation)
{
    merge_action_observation(observation);
}

bridge_action_result_t bridge_action_read(const char *entity, int timeout_ms)
{
    return action_http(entity,nullptr,timeout_ms);
}

void bridge_toggle_light(const char *entity_id)
{
    if (!wifi_is_connected() || !entity_id) return;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/ha/action", s_bridge_url);

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
    esp_http_client_set_header(client, "X-API-Key", s_bridge_key);
    if (web_identity_set_http_header(client) != ESP_OK) {
        esp_http_client_cleanup(client);
        free(resp.data);
        return;
    }
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
                xSemaphoreTake(s_data_mutex, portMAX_DELAY);
                parse_lights(lights);
                xSemaphoreGive(s_data_mutex);
            }
            cJSON_Delete(root);
        }
    } else {
        ESP_LOGE(TAG, "Toggle %s failed: HTTP %d", entity_id, status);
    }
    free(resp.data);
}

void bridge_climate_action(const char *entity_id, const char *action, float temperature, const char *hvac_mode)
{
    if (!wifi_is_connected() || !entity_id || !action) return;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/ha/action", s_bridge_url);
    char body[192];
    if (strcmp(action, "set_temperature") == 0) {
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"action\":\"set_temperature\",\"temperature\":%.1f}", entity_id, temperature);
    } else if (strcmp(action, "set_hvac_mode") == 0 && hvac_mode) {
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"action\":\"set_hvac_mode\",\"hvac_mode\":\"%s\"}", entity_id, hvac_mode);
    } else {
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",\"action\":\"%s\"}", entity_id, action);
    }

    http_buf_t resp = {};
    resp.cap = 2048;
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
    esp_http_client_set_header(client, "X-API-Key", s_bridge_key);
    if (web_identity_set_http_header(client) != ESP_OK) {
        esp_http_client_cleanup(client);
        free(resp.data);
        return;
    }
    esp_http_client_set_post_field(client, body, strlen(body));
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200 && resp.data) {
        cJSON *root = cJSON_Parse(resp.data);
        if (root) {
            cJSON *climate = cJSON_GetObjectItem(root, "climate");
            if (climate) {
                xSemaphoreTake(s_data_mutex, portMAX_DELAY);
                parse_climate(climate);
                xSemaphoreGive(s_data_mutex);
            }
            cJSON_Delete(root);
        }
        ESP_LOGI(TAG, "Climate %s %s OK", entity_id, action);
    } else {
        ESP_LOGE(TAG, "Climate %s %s failed: HTTP %d", entity_id, action, status);
    }
    free(resp.data);
}

static void mark_calendar_failure(int year, int month, int day) {
    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    if (s_cal_data.year != year || s_cal_data.month != month || s_cal_data.day != day) {
        s_cal_data = {};
        s_cal_data.year = year; s_cal_data.month = month; s_cal_data.day = day;
    }
    s_cal_data.last_attempt_failed = true;
    xSemaphoreGive(s_data_mutex);
}

void bridge_fetch_calendar(int year, int month, int day)
{
    if (!wifi_is_connected()) { mark_calendar_failure(year, month, day); return; }

    char url[256];
    snprintf(url, sizeof(url), "%s/api/calendar?date=%04d-%02d-%02d", s_bridge_url, year, month, day);

    http_buf_t resp = {};
    char cal_err[32];
    if (!http_get(url, &resp, cal_err, sizeof(cal_err))) {
        ESP_LOGE(TAG, "Calendar fetch failed: %s", cal_err);
        mark_calendar_failure(year, month, day);
        return;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);
    if (!root || !cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        mark_calendar_failure(year, month, day);
        return;
    }

    xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    s_cal_data.count = 0;
    s_cal_data.year = year;
    s_cal_data.month = month;
    s_cal_data.day = day;

    int n = cJSON_GetArraySize(root);
    s_cal_data.events.clear();
    s_cal_data.events.resize(n);

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
    s_cal_data.received_ms = esp_timer_get_time() / 1000;
    s_cal_data.last_attempt_failed = false;
    s_cal_data.completeness_known = resp.calendar_state != 0;
    s_cal_data.sources_partial = resp.calendar_state == 2;
    xSemaphoreGive(s_data_mutex);
    cJSON_Delete(root);
}
