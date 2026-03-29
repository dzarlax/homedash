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

static bridge_data_t s_data = {};
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
            strncpy(t->title, title->valuestring, sizeof(t->title) - 1);
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
            strncpy(nw->title, title->valuestring, sizeof(nw->title) - 1);
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

void bridge_fetch_and_update(void)
{
    if (!wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "No WiFi");
        return;
    }

    snprintf(s_last_error, sizeof(s_last_error), "Fetching...");

    http_buf_t resp = {};
    resp.cap = 4096;
    resp.data = (char *)malloc(resp.cap);
    if (!resp.data) {
        snprintf(s_last_error, sizeof(s_last_error), "OOM");
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/api/dashboard?key=%s", BRIDGE_URL, BRIDGE_API_KEY);

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.user_data = &resp;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        free(resp.data);
        snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", status);
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

    cJSON_Delete(root);
    snprintf(s_last_error, sizeof(s_last_error), "OK");
}

const bridge_data_t *bridge_get_data(void)
{
    return &s_data;
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

    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err == ESP_OK && status == 200) {
        ESP_LOGI(TAG, "Toggle %s OK", entity_id);
        // Refresh data immediately
        bridge_fetch_and_update();
    } else {
        ESP_LOGE(TAG, "Toggle %s failed: HTTP %d", entity_id, status);
    }
}
