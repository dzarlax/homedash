#include "wifi_manager.h"

#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group = NULL;
static bool s_wifi_started = false;
static bool s_wifi_stack_ready = false;
static bool s_sta_configured = false;
static bool s_recovery_ap_started = false;
static esp_netif_t *s_ap_netif = NULL;
static bool s_sntp_started = false;
static char s_timezone[48] = {};
static const char *s_ntp_server = NULL;
static wifi_config_t s_trial_previous = {};
static bool s_trial_active = false;

static void start_sntp_once(void)
{
    if (s_sntp_started || !s_ntp_server) return;
    setenv("TZ", s_timezone[0] ? s_timezone : "UTC0", 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(s_ntp_server);
    esp_netif_sntp_init(&cfg);
    s_sntp_started = true;
    ESP_LOGI(TAG, "SNTP started");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (s_sta_configured) esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_sta_configured) {
                ESP_LOGW(TAG, "Disconnected, reconnecting...");
                esp_wifi_connect();
            }
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        start_sntp_once();
    }
}

static bool wifi_stack_init(void)
{
    if (s_wifi_stack_ready) return true;
    s_wifi_event_group = xEventGroupCreate();
    if (!s_wifi_event_group) return false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return false;
    if (!esp_netif_create_default_wifi_sta()) return false;
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK || esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK)
        return false;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
            &wifi_event_handler, NULL, NULL) != ESP_OK) return false;
    if (esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
            &wifi_event_handler, NULL, NULL) != ESP_OK) return false;
    s_wifi_stack_ready = true;
    return true;
}

void wifi_init(const char *ssid, const char *password, const char *timezone, const char *ntp_server)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGW(TAG, "SSID not configured, skipping");
        return;
    }

    if (!wifi_stack_init()) {
        ESP_LOGE(TAG, "WiFi stack initialization failed");
        return;
    }
    strncpy(s_timezone, timezone ? timezone : "UTC0", sizeof(s_timezone) - 1);
    s_ntp_server = ntp_server;

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_started = true;
    s_sta_configured = true;
    ESP_LOGI(TAG, "Connecting to configured Wi-Fi...");

    // Wait up to 10 seconds for initial connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
        WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "Initial connection timed out, will retry in background");
    }
}

bool wifi_is_connected(void)
{
    if (!s_wifi_started || !s_wifi_event_group) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

bool wifi_time_is_valid(void)
{
    time_t now; time(&now);
    struct tm t = {}; localtime_r(&now, &t);
    return t.tm_year >= 100;
}

void wifi_set_timezone(const char *timezone)
{
    if (!timezone || !timezone[0] || strlen(timezone) >= sizeof(s_timezone)) return;
    strncpy(s_timezone, timezone, sizeof(s_timezone) - 1);
    s_timezone[sizeof(s_timezone) - 1] = 0;
    setenv("TZ", s_timezone, 1);
    tzset();
}

bool wifi_begin_trial(const char *ssid, const char *password, uint32_t timeout_ms)
{
    if (!s_wifi_stack_ready || !ssid || !ssid[0] || strlen(ssid) > 32 ||
        !password || strlen(password) > 64 || s_trial_active) return false;
    if (esp_wifi_get_config(WIFI_IF_STA, &s_trial_previous) != ESP_OK) return false;
    wifi_config_t candidate = {};
    memcpy(candidate.sta.ssid, ssid, strlen(ssid));
    memcpy(candidate.sta.password, password, strlen(password));
    if (s_wifi_started) esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    wifi_mode_t mode = s_recovery_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    if (esp_wifi_set_mode(mode) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_STA, &candidate) != ESP_OK ||
        (!s_wifi_started && esp_wifi_start() != ESP_OK) ||
        esp_wifi_connect() != ESP_OK) {
        esp_wifi_set_config(WIFI_IF_STA, &s_trial_previous);
        s_sta_configured = s_trial_previous.sta.ssid[0] != 0;
        if (s_sta_configured) esp_wifi_connect();
        return false;
    }
    s_wifi_started = true;
    s_sta_configured = true;
    s_trial_active = true;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        wifi_rollback_trial();
        return false;
    }
    return true;
}

void wifi_rollback_trial(void)
{
    if (!s_trial_active) return;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &s_trial_previous);
    s_sta_configured = s_trial_previous.sta.ssid[0] != 0;
    if (err == ESP_OK && s_sta_configured) esp_wifi_connect();
    s_trial_active = false;
}

void wifi_confirm_trial(void)
{
    s_trial_active = false;
    memset(&s_trial_previous, 0, sizeof(s_trial_previous));
}

bool wifi_start_recovery_ap(const char *ssid, const char *password)
{
    if (!ssid || strlen(ssid) > 32 || !password || strlen(password) < 8 ||
        strlen(password) > 63 || !wifi_stack_init()) return false;
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();
    if (!s_ap_netif) return false;
    wifi_config_t ap = {};
    strncpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid) - 1);
    strncpy((char *)ap.ap.password, password, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 2;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    if (esp_wifi_set_mode(s_sta_configured ? WIFI_MODE_APSTA : WIFI_MODE_AP) != ESP_OK ||
        esp_wifi_set_config(WIFI_IF_AP, &ap) != ESP_OK) return false;
    if (!s_wifi_started && esp_wifi_start() != ESP_OK) return false;
    s_wifi_started = true;
    s_recovery_ap_started = true;
    return true;
}

void wifi_forget_station(void)
{
    if (!s_wifi_stack_ready) return;
    wifi_config_t empty = {};
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    s_sta_configured = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
}

void wifi_stop_recovery_ap(void)
{
    if (!s_recovery_ap_started) return;
    if (s_sta_configured) esp_wifi_set_mode(WIFI_MODE_STA);
    else {
        esp_wifi_set_mode(WIFI_MODE_NULL);
        esp_wifi_stop();
        s_wifi_started = false;
    }
    s_recovery_ap_started = false;
}
