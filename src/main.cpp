#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif_sntp.h"
#include "nvs_flash.h"

#include "lvgl_port.h"
#include "gt911.h"
#include "config.h"
#include "wifi_manager.h"
#include "weather.h"
#include "ha_calendar.h"
#include "transport.h"
#include "ui_dashboard.h"

static const char *TAG = "main";

// Network task handle for cross-core notification
static TaskHandle_t s_network_task_handle = NULL;

// Shared date request (written by UI core, read by network core)
static volatile int  s_req_year  = 0;
static volatile int  s_req_month = 0;
static volatile int  s_req_day   = 0;
static volatile bool s_date_requested = false;

void request_calendar_date(int year, int month, int day)
{
    s_req_year  = year;
    s_req_month = month;
    s_req_day   = day;
    s_date_requested = true;
    if (s_network_task_handle) {
        xTaskNotifyGive(s_network_task_handle);
    }
}

// Background task: WiFi + NTP + Weather (runs on core 0)
static void network_task(void *param)
{
    // Wait for LVGL to render first frame
    vTaskDelay(pdMS_TO_TICKS(2000));

    // WiFi init
    wifi_init(WIFI_SSID, WIFI_PASSWORD);

    // NTP
    if (wifi_is_connected()) {
        setenv("TZ", POSIX_TZ, 1);
        tzset();

        esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
        esp_netif_sntp_init(&sntp_cfg);

        struct tm tinfo = {};
        int tries = 30;
        while (tinfo.tm_year < 100 && tries-- > 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            time_t now;
            time(&now);
            localtime_r(&now, &tinfo);
        }
        ESP_LOGI(TAG, "NTP sync %s (year=%d)", tinfo.tm_year > 100 ? "OK" : "FAILED", tinfo.tm_year + 1900);
    }

    // Tick-based scheduler: weather every 30min, HA calendar every 5min
    vTaskDelay(pdMS_TO_TICKS(1000));

    const uint32_t TICK_MS = 10000;
    const uint32_t TRANSPORT_UPDATE_MS = 60000;  // every 1 minute
    uint32_t weather_elapsed   = WEATHER_UPDATE_INTERVAL_MS;  // trigger immediately
    uint32_t ha_cal_elapsed    = HA_CAL_UPDATE_INTERVAL_MS;   // trigger immediately
    uint32_t transport_elapsed = TRANSPORT_UPDATE_MS;          // trigger immediately

    for (;;) {
        if (weather_elapsed >= WEATHER_UPDATE_INTERVAL_MS) {
            weather_fetch_and_update();
            weather_elapsed = 0;
        }

        // Check for on-demand date request (from calendar click)
        if (s_date_requested) {
            int y = s_req_year;
            int m = s_req_month;
            int d = s_req_day;
            s_date_requested = false;
            ha_calendar_fetch_for_date(y, m, d);
            ha_cal_elapsed = 0; // reset auto-refresh timer
        } else if (ha_cal_elapsed >= HA_CAL_UPDATE_INTERVAL_MS) {
            ha_calendar_fetch_and_update();
            ha_cal_elapsed = 0;
        }

        if (transport_elapsed >= TRANSPORT_UPDATE_MS) {
            transport_fetch_and_update();
            transport_elapsed = 0;
        }

        // Wait for notification (instant wake) or timeout after TICK_MS
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS));
        weather_elapsed   += TICK_MS;
        ha_cal_elapsed    += TICK_MS;
        transport_elapsed += TICK_MS;
    }
}

extern "C" void app_main(void)
{
    // Initialize NVS (required by WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    vTaskDelay(pdMS_TO_TICKS(100));

    static esp_lcd_panel_handle_t panel_handle = NULL;
    static esp_lcd_touch_handle_t tp_handle = NULL;

    // 1. Hardware init
    tp_handle = touch_gt911_init();
    panel_handle = waveshare_esp32_s3_rgb_lcd_init();
    wavesahre_rgb_lcd_bl_on();
    ESP_ERROR_CHECK(lvgl_port_init(panel_handle, tp_handle));

    // 2. Create dashboard UI
    if (lvgl_port_lock(-1)) {
        ui_dashboard_create();
        lvgl_port_unlock();
    }

    // 3. Start LVGL render loop
    lvgl_port_task_start();

    // 4. Network task on core 0
    xTaskCreatePinnedToCore(network_task, "network", 16 * 1024, NULL, 1, &s_network_task_handle, 0);
}
