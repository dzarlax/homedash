#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
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
enum request_kind_t { REQUEST_CALENDAR, REQUEST_TOGGLE, REQUEST_OTA };
struct network_request_t { request_kind_t kind; int year, month, day; char entity[48]; };
static QueueHandle_t s_request_queue = NULL;

void request_calendar_date(int year, int month, int day)
{
    network_request_t req = { REQUEST_CALENDAR, year, month, day, {} };
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

// Shared light toggle request (written by UI core, read by network core)
void request_light_toggle(const char *entity_id)
{
    network_request_t req = { REQUEST_TOGGLE, 0, 0, 0, {} };
    if (entity_id) strncpy(req.entity, entity_id, sizeof(req.entity) - 1);
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
}

// Manual OTA check request (from UI button)
void request_ota_check(void)
{
    network_request_t req = { REQUEST_OTA, 0, 0, 0, {} };
    if (s_request_queue) xQueueSend(s_request_queue, &req, 0);
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
    wifi_init(cfg.wifi_ssid, cfg.wifi_pass, POSIX_TZ, NTP_SERVER);

    vTaskDelay(pdMS_TO_TICKS(1000));

    const uint32_t TICK_MS = 10000;
    uint32_t bridge_elapsed = BRIDGE_UPDATE_INTERVAL_MS; // trigger immediately
    uint32_t ota_elapsed = OTA_CHECK_INTERVAL_MS;        // trigger immediately
    bool cal_initial = true;
    bool app_validated = false;

    for (;;) {
        network_request_t req = {};
        while (xQueueReceive(s_request_queue, &req, 0) == pdTRUE) {
            if (req.kind == REQUEST_CALENDAR) bridge_fetch_calendar(req.year, req.month, req.day);
            else if (req.kind == REQUEST_TOGGLE) bridge_toggle_light(req.entity);
            else ota_check_and_update();
        }

        // Bridge polling
        if (bridge_elapsed >= BRIDGE_UPDATE_INTERVAL_MS) {
            bridge_fetch_and_update();
            bridge_elapsed = 0;

            // Mark app as valid after first successful bridge fetch (rollback protection)
            bridge_data_t snapshot = {};
            bridge_copy_data(&snapshot);
            if (!app_validated && snapshot.ts > 0) {
                esp_ota_mark_app_valid_cancel_rollback();
                app_validated = true;
                ESP_LOGI(TAG, "App marked valid (rollback cancelled)");
            }

            // Fetch today's calendar after first successful bridge update
            if (cal_initial && snapshot.ts > 0 && wifi_time_is_valid()) {
                time_t now;
                time(&now);
                struct tm t;
                localtime_r(&now, &t);
                bridge_fetch_calendar(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
                cal_initial = false;
            }
        }

        // Periodic OTA check
        if (ota_elapsed >= OTA_CHECK_INTERVAL_MS) {
            ota_check_and_update();
            ota_elapsed = 0;
        }

        TickType_t started = xTaskGetTickCount();
        if (xQueueReceive(s_request_queue, &req, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            if (req.kind == REQUEST_CALENDAR) bridge_fetch_calendar(req.year, req.month, req.day);
            else if (req.kind == REQUEST_TOGGLE) bridge_toggle_light(req.entity);
            else ota_check_and_update();
        }
        uint32_t elapsed = (xTaskGetTickCount() - started) * portTICK_PERIOD_MS;
        bridge_elapsed += elapsed;
        ota_elapsed += elapsed;
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
    s_request_queue = xQueueCreate(8, sizeof(network_request_t));
    configASSERT(s_request_queue);
    xTaskCreatePinnedToCore(network_task, "network", 32 * 1024, NULL, 1, NULL, 0);
}
