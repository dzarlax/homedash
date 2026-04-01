#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif_sntp.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

#include "lvgl_port.h"
#include "gt911.h"
#include "config.h"
#include "nvs_config.h"
#include "wifi_manager.h"
#include "bridge.h"
#include "ota.h"
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

// Shared light toggle request (written by UI core, read by network core)
static char s_toggle_entity[48] = {};
static volatile bool s_toggle_requested = false;

void request_light_toggle(const char *entity_id)
{
    strncpy(s_toggle_entity, entity_id, sizeof(s_toggle_entity) - 1);
    s_toggle_entity[sizeof(s_toggle_entity) - 1] = '\0';
    s_toggle_requested = true;
    if (s_network_task_handle) {
        xTaskNotifyGive(s_network_task_handle);
    }
}

// Background task: WiFi + NTP + Bridge polling + OTA (runs on core 0)
static void network_task(void *param)
{
    // Wait for LVGL to render first frame
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Load config from NVS
    device_config_t cfg;
    if (!nvs_config_load(&cfg)) {
        ESP_LOGE(TAG, "NVS config missing! Flash nvs.bin first. Halting network.");
        for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
    }

    // Initialize bridge with runtime URL
    bridge_init(cfg.bridge_url, cfg.bridge_key);
    ota_init(cfg.bridge_url, cfg.bridge_key);

    // WiFi init
    wifi_init(cfg.wifi_ssid, cfg.wifi_pass);

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

    vTaskDelay(pdMS_TO_TICKS(1000));

    const uint32_t TICK_MS = 10000;
    uint32_t bridge_elapsed = BRIDGE_UPDATE_INTERVAL_MS; // trigger immediately
    uint32_t ota_elapsed = OTA_CHECK_INTERVAL_MS;        // trigger immediately
    bool cal_initial = true;
    bool app_validated = false;

    for (;;) {
        // On-demand calendar date request (from UI calendar tap)
        if (s_date_requested) {
            int y = s_req_year;
            int m = s_req_month;
            int d = s_req_day;
            s_date_requested = false;
            bridge_fetch_calendar(y, m, d);
        }

        // On-demand light toggle (from UI tap)
        if (s_toggle_requested) {
            s_toggle_requested = false;
            bridge_toggle_light(s_toggle_entity);
        }

        // Bridge polling
        if (bridge_elapsed >= BRIDGE_UPDATE_INTERVAL_MS) {
            bridge_fetch_and_update();
            bridge_elapsed = 0;

            // Mark app as valid after first successful bridge fetch (rollback protection)
            if (!app_validated && bridge_get_data()->ts > 0) {
                esp_ota_mark_app_valid_cancel_rollback();
                app_validated = true;
                ESP_LOGI(TAG, "App marked valid (rollback cancelled)");
            }

            // Fetch today's calendar after first successful bridge update
            if (cal_initial && bridge_get_data()->ts > 0) {
                time_t now;
                time(&now);
                struct tm t;
                localtime_r(&now, &t);
                bridge_fetch_calendar(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
                cal_initial = false;
            }
        }

        // OTA check
        if (ota_elapsed >= OTA_CHECK_INTERVAL_MS) {
            ota_check_and_update(); // reboots if update found
            ota_elapsed = 0;
        }

        // Wait for notification (instant wake on UI request) or timeout
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS));
        bridge_elapsed += TICK_MS;
        ota_elapsed += TICK_MS;
    }
}

extern "C" void app_main(void)
{
    // Initialize NVS (required by WiFi and config)
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
