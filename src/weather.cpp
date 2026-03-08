#include "weather.h"
#include "config.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "weather";

static weather_data_t s_weather = {};
static char s_last_error[128] = "Waiting...";

static const char *WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=44.82&longitude=20.46"
    "&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&timezone=Europe%2FBelgrade"
    "&forecast_days=5";

// Dynamic buffer for HTTP response
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

const char *weather_code_to_text(int code)
{
    if (code == 0)                      return "Clear";
    if (code >= 1 && code <= 3)         return "Cloudy";
    if (code == 45 || code == 48)       return "Fog";
    if (code >= 51 && code <= 55)       return "Drizzle";
    if (code >= 61 && code <= 65)       return "Rain";
    if (code >= 71 && code <= 75)       return "Snow";
    if (code >= 80 && code <= 82)       return "Showers";
    if (code >= 95 && code <= 99)       return "Storm";
    return "Unknown";
}

void weather_fetch_and_update(void)
{
    if (!wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "No WiFi");
        return;
    }

    snprintf(s_last_error, sizeof(s_last_error), "Fetching...");

    http_buf_t resp = {};
    resp.cap = 2048;
    resp.data = (char *)malloc(resp.cap);
    if (!resp.data) {
        snprintf(s_last_error, sizeof(s_last_error), "OOM");
        return;
    }

    esp_http_client_config_t config = {};
    config.url = WEATHER_URL;
    config.event_handler = http_event_handler;
    config.user_data = &resp;
    config.timeout_ms = 15000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "HTTP err: %s", esp_err_to_name(err));
        free(resp.data);
        return;
    }
    if (status != 200) {
        snprintf(s_last_error, sizeof(s_last_error), "HTTP %d", status);
        free(resp.data);
        return;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);

    if (!root) {
        snprintf(s_last_error, sizeof(s_last_error), "JSON parse failed");
        return;
    }

    cJSON *current = cJSON_GetObjectItem(root, "current");
    if (current) {
        cJSON *item;
        item = cJSON_GetObjectItem(current, "temperature_2m");
        s_weather.temp = item ? (float)item->valuedouble : 0.0f;
        item = cJSON_GetObjectItem(current, "relative_humidity_2m");
        s_weather.humidity = item ? (float)item->valuedouble : 0.0f;
        item = cJSON_GetObjectItem(current, "wind_speed_10m");
        s_weather.wind_speed = item ? (float)item->valuedouble : 0.0f;
        item = cJSON_GetObjectItem(current, "weather_code");
        s_weather.weather_code = item ? item->valueint : 0;
    }

    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily) {
        cJSON *codes    = cJSON_GetObjectItem(daily, "weather_code");
        cJSON *temp_max = cJSON_GetObjectItem(daily, "temperature_2m_max");
        cJSON *temp_min = cJSON_GetObjectItem(daily, "temperature_2m_min");

        int count = cJSON_GetArraySize(codes);
        for (int i = 0; i < WEATHER_FORECAST_DAYS && i < count; i++) {
            cJSON *c = cJSON_GetArrayItem(codes, i);
            cJSON *mx = cJSON_GetArrayItem(temp_max, i);
            cJSON *mn = cJSON_GetArrayItem(temp_min, i);
            s_weather.daily[i].weather_code = c ? c->valueint : 0;
            s_weather.daily[i].temp_max     = mx ? (float)mx->valuedouble : 0.0f;
            s_weather.daily[i].temp_min     = mn ? (float)mn->valuedouble : 0.0f;
        }
    }
    s_weather.valid = true;

    cJSON_Delete(root);

    snprintf(s_last_error, sizeof(s_last_error), "OK: %.1fC %s",
             s_weather.temp, weather_code_to_text(s_weather.weather_code));
}

const weather_data_t *weather_get_data(void)
{
    return &s_weather;
}

const char *weather_get_last_error(void)
{
    return s_last_error;
}
