#include "transport.h"
#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "transport";

static transport_data_t s_transport = {};
static char s_last_error[64] = "Waiting...";

static const char *STOP_URLS[TRANSPORT_NUM_STOPS] = {
    "https://transport-api.dzarlax.dev/api/stations/bg/search?id=89",
    "https://transport-api.dzarlax.dev/api/stations/bg/search?id=90",
};

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

static bool fetch_stop(int stop_idx)
{
    http_buf_t resp = {};
    resp.cap = 2048;
    resp.data = (char *)malloc(resp.cap);
    if (!resp.data) return false;

    esp_http_client_config_t config = {};
    config.url = STOP_URLS[stop_idx];
    config.event_handler = http_event_handler;
    config.user_data = &resp;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        free(resp.data);
        return false;
    }

    cJSON *root = cJSON_Parse(resp.data);
    free(resp.data);
    if (!root) return false;

    transport_stop_t *stop = &s_transport.stops[stop_idx];
    stop->count = 0;

    cJSON *vehicles = cJSON_GetObjectItem(root, "vehicles");
    if (vehicles && cJSON_IsArray(vehicles)) {
        int arr_size = cJSON_GetArraySize(vehicles);
        for (int i = 0; i < arr_size && stop->count < TRANSPORT_MAX_VEHICLES; i++) {
            cJSON *v = cJSON_GetArrayItem(vehicles, i);
            if (!v) continue;

            cJSON *ln = cJSON_GetObjectItem(v, "lineNumber");
            cJSON *sl = cJSON_GetObjectItem(v, "secondsLeft");
            cJSON *sb = cJSON_GetObjectItem(v, "stationsBetween");

            if (!ln || !sl) continue;

            transport_vehicle_t *tv = &stop->vehicles[stop->count];
            const char *line_str = cJSON_GetStringValue(ln);
            if (line_str) {
                strncpy(tv->line_number, line_str, sizeof(tv->line_number) - 1);
                tv->line_number[sizeof(tv->line_number) - 1] = '\0';
            }
            tv->seconds_left = sl->valueint;
            tv->stations_between = sb ? sb->valueint : 0;
            stop->count++;
        }
    }

    cJSON_Delete(root);

    // Sort vehicles by arrival time (fastest first)
    for (int i = 0; i < stop->count - 1; i++) {
        for (int j = i + 1; j < stop->count; j++) {
            if (stop->vehicles[j].seconds_left < stop->vehicles[i].seconds_left) {
                transport_vehicle_t tmp = stop->vehicles[i];
                stop->vehicles[i] = stop->vehicles[j];
                stop->vehicles[j] = tmp;
            }
        }
    }

    return true;
}

void transport_fetch_and_update(void)
{
    if (!wifi_is_connected()) {
        snprintf(s_last_error, sizeof(s_last_error), "No WiFi");
        return;
    }

    snprintf(s_last_error, sizeof(s_last_error), "Fetching...");

    bool ok0 = fetch_stop(TRANSPORT_STOP_OUT);
    bool ok1 = fetch_stop(TRANSPORT_STOP_IN);

    if (ok0 || ok1) {
        s_transport.valid = true;
        snprintf(s_last_error, sizeof(s_last_error), "OK");
    } else {
        snprintf(s_last_error, sizeof(s_last_error), "Fetch failed");
    }
}

const transport_data_t *transport_get_data(void)
{
    return &s_transport;
}

const char *transport_get_last_error(void)
{
    return s_last_error;
}
