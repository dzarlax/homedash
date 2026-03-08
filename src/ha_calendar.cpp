#include "ha_calendar.h"
#include "config.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"

static const char *TAG = "ha_cal";

static ha_cal_data_t s_ha_cal = {};
static char s_last_error[128] = "Waiting...";

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

// Fetch a URL with HA bearer token, return malloc'd response (caller frees)
static char *ha_fetch(const char *url)
{
    http_buf_t resp = {};
    resp.cap = 2048;
    resp.data = (char *)malloc(resp.cap);
    if (!resp.data) return NULL;

    esp_http_client_config_t config = {};
    config.url = url;
    config.event_handler = http_event_handler;
    config.user_data = &resp;
    config.timeout_ms = 15000;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        free(resp.data);
        return NULL;
    }

    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", HA_TOKEN);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "HTTP %s status=%d url=%s",
                 err != ESP_OK ? esp_err_to_name(err) : "OK", status, url);
        free(resp.data);
        return NULL;
    }

    return resp.data;
}

// Parse "2026-02-22T09:30:00+01:00" or "2026-02-22T09:30:00" into hour/min
static void parse_datetime(const char *str, uint8_t *hour, uint8_t *min)
{
    *hour = 0;
    *min = 0;
    if (!str) return;
    // Find the 'T' separator
    const char *t = strchr(str, 'T');
    if (!t) return;
    t++;
    int h = 0, m = 0;
    if (sscanf(t, "%d:%d", &h, &m) >= 2) {
        *hour = (uint8_t)h;
        *min  = (uint8_t)m;
    }
}

static int event_compare(const void *a, const void *b)
{
    const ha_cal_event_t *ea = (const ha_cal_event_t *)a;
    const ha_cal_event_t *eb = (const ha_cal_event_t *)b;
    return ea->sort_key - eb->sort_key;
}

// Internal fetch for a specific date
static void ha_cal_fetch_internal(int year, int month, int day)
{
    if (!wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "No WiFi");
        return;
    }

    ESP_LOGI(TAG, "Fetching events for %04d-%02d-%02d", year, month, day);
    snprintf(s_last_error, sizeof(s_last_error), "Fetching...");

    // Phase 1: Get list of calendars
    char url[512];
    snprintf(url, sizeof(url), "%s/api/calendars", HA_BASE_URL);

    char *cal_list_json = ha_fetch(url);
    if (!cal_list_json) {
        snprintf(s_last_error, sizeof(s_last_error), "Failed to fetch calendar list");
        return;
    }

    cJSON *cal_list = cJSON_Parse(cal_list_json);
    free(cal_list_json);
    if (!cal_list || !cJSON_IsArray(cal_list)) {
        snprintf(s_last_error, sizeof(s_last_error), "Bad calendar list JSON");
        if (cal_list) cJSON_Delete(cal_list);
        return;
    }

    // Build date range for the requested day
    char start_date[32], end_date[32];
    snprintf(start_date, sizeof(start_date), "%04d-%02d-%02dT00:00:00",
             year, month, day);

    // End = next day. Simple day+1 (handles month rollover via HA API tolerance)
    int next_day = day + 1;
    int next_month = month;
    int next_year = year;
    // Days in month (approximate — HA API is forgiving)
    int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) days_in_month[2] = 29;
    if (next_day > days_in_month[month]) {
        next_day = 1;
        next_month++;
        if (next_month > 12) {
            next_month = 1;
            next_year++;
        }
    }
    snprintf(end_date, sizeof(end_date), "%04d-%02d-%02dT00:00:00",
             next_year, next_month, next_day);

    ha_cal_data_t tmp = {};
    tmp.year  = year;
    tmp.month = month;
    tmp.day   = day;

    // Phase 2: For each calendar, fetch events for the requested day
    int cal_count = cJSON_GetArraySize(cal_list);
    uint8_t cal_idx = 0;
    for (int c = 0; c < cal_count && tmp.count < HA_CAL_MAX_EVENTS; c++) {
        cJSON *cal = cJSON_GetArrayItem(cal_list, c);
        cJSON *entity_id = cJSON_GetObjectItem(cal, "entity_id");
        if (!entity_id || !entity_id->valuestring) continue;

        // Skip workday sensor calendar
        if (strstr(entity_id->valuestring, "workday_sensor") != NULL) {
            ESP_LOGI(TAG, "Skipping calendar: %s", entity_id->valuestring);
            continue;
        }

        snprintf(url, sizeof(url), "%s/api/calendars/%s?start=%s&end=%s",
                 HA_BASE_URL, entity_id->valuestring, start_date, end_date);

        char *events_json = ha_fetch(url);
        if (!events_json) continue;

        cJSON *events = cJSON_Parse(events_json);
        free(events_json);
        if (!events || !cJSON_IsArray(events)) {
            if (events) cJSON_Delete(events);
            continue;
        }

        int ev_count = cJSON_GetArraySize(events);
        for (int e = 0; e < ev_count && tmp.count < HA_CAL_MAX_EVENTS; e++) {
            cJSON *ev = cJSON_GetArrayItem(events, e);
            cJSON *summary = cJSON_GetObjectItem(ev, "summary");
            cJSON *start_obj = cJSON_GetObjectItem(ev, "start");

            if (!summary || !summary->valuestring || !start_obj) continue;

            ha_cal_event_t *out = &tmp.events[tmp.count];
            memset(out, 0, sizeof(*out));
            out->cal_idx = cal_idx;
            strncpy(out->summary, summary->valuestring, sizeof(out->summary) - 1);

            // Check if all-day (has "date" field) or timed (has "dateTime" field)
            cJSON *start_date_field = cJSON_GetObjectItem(start_obj, "date");
            cJSON *start_datetime = cJSON_GetObjectItem(start_obj, "dateTime");

            if (start_date_field && start_date_field->valuestring) {
                out->all_day = true;
                out->sort_key = 0;
            } else if (start_datetime && start_datetime->valuestring) {
                parse_datetime(start_datetime->valuestring, &out->start_hour, &out->start_min);
                out->sort_key = out->start_hour * 100 + out->start_min;

                cJSON *end_obj = cJSON_GetObjectItem(ev, "end");
                if (end_obj) {
                    cJSON *end_datetime = cJSON_GetObjectItem(end_obj, "dateTime");
                    if (end_datetime && end_datetime->valuestring) {
                        parse_datetime(end_datetime->valuestring, &out->end_hour, &out->end_min);
                    }
                }
            }

            tmp.count++;
        }

        cJSON_Delete(events);
        cal_idx++;
    }

    cJSON_Delete(cal_list);

    // Sort: all-day first (sort_key=0), then by start time
    if (tmp.count > 1) {
        qsort(tmp.events, tmp.count, sizeof(ha_cal_event_t), event_compare);
    }

    tmp.valid = true;
    s_ha_cal = tmp;

    snprintf(s_last_error, sizeof(s_last_error), "OK: %d events", s_ha_cal.count);
    ESP_LOGI(TAG, "Fetched %d events for %04d-%02d-%02d", s_ha_cal.count, year, month, day);
}

void ha_calendar_fetch_and_update(void)
{
    time_t now;
    time(&now);
    struct tm tinfo;
    localtime_r(&now, &tinfo);
    ha_cal_fetch_internal(tinfo.tm_year + 1900, tinfo.tm_mon + 1, tinfo.tm_mday);
}

void ha_calendar_fetch_for_date(int year, int month, int day)
{
    ha_cal_fetch_internal(year, month, day);
}

const ha_cal_data_t *ha_calendar_get_data(void)
{
    return &s_ha_cal;
}

const char *ha_calendar_get_last_error(void)
{
    return s_last_error;
}
