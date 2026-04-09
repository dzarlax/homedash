#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "lvgl_port.h"

static const char *TAG = "lv_port";
static SemaphoreHandle_t lvgl_mux;
static TaskHandle_t lvgl_task_handle = NULL;
static bool lvgl_task_running = false;

/* ---------------------------------------------------------------
 * Flush callbacks — LVGL 9 API: (lv_display_t*, area, px_map)
 * --------------------------------------------------------------- */

#if LVGL_PORT_AVOID_TEAR_ENABLE && LVGL_PORT_DIRECT_MODE && (EXAMPLE_LVGL_PORT_ROTATION_DEGREE == 0)

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);

    if (lv_display_flush_is_last(disp)) {
        esp_lcd_panel_draw_bitmap(panel_handle,
                                  area->x1, area->y1,
                                  area->x2 + 1, area->y2 + 1,
                                  px_map);
        ulTaskNotifyValueClear(NULL, ULONG_MAX);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    lv_display_flush_ready(disp);
}

#elif LVGL_PORT_AVOID_TEAR_ENABLE && LVGL_PORT_FULL_REFRESH && (LVGL_PORT_LCD_RGB_BUFFER_NUMS == 2)

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    ulTaskNotifyValueClear(NULL, ULONG_MAX);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    lv_display_flush_ready(disp);
}

#else

static void flush_callback(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    esp_lcd_panel_draw_bitmap(panel_handle,
                              area->x1, area->y1,
                              area->x2 + 1, area->y2 + 1,
                              px_map);
    lv_display_flush_ready(disp);
}

#endif

/* ---------------------------------------------------------------
 * Display init — LVGL 9 API
 * --------------------------------------------------------------- */
static lv_display_t *display_init(esp_lcd_panel_handle_t panel_handle)
{
    assert(panel_handle);

    void *buf1 = NULL;
    void *buf2 = NULL;
    size_t buffer_size = 0;

    ESP_LOGD(TAG, "Allocating LVGL framebuffers");
#if LVGL_PORT_AVOID_TEAR_ENABLE
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_V_RES;
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
#else
    buffer_size = LVGL_PORT_H_RES * LVGL_PORT_BUFFER_HEIGHT;
    buf1 = heap_caps_malloc(buffer_size * sizeof(lv_color_t), LVGL_PORT_BUFFER_MALLOC_CAPS);
    assert(buf1);
    ESP_LOGI(TAG, "LVGL buffer size: %uKB", (unsigned)(buffer_size * sizeof(lv_color_t) / 1024));
#endif

    lv_display_t *disp = lv_display_create(LVGL_PORT_H_RES, LVGL_PORT_V_RES);
    assert(disp);

    lv_display_set_flush_cb(disp, flush_callback);
    lv_display_set_user_data(disp, panel_handle);

#if LVGL_PORT_DIRECT_MODE
    lv_display_set_buffers(disp, buf1, buf2,
                           buffer_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
#elif LVGL_PORT_FULL_REFRESH
    lv_display_set_buffers(disp, buf1, buf2,
                           buffer_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_FULL);
#else
    lv_display_set_buffers(disp, buf1, buf2,
                           buffer_size * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
#endif

    return disp;
}

/* ---------------------------------------------------------------
 * Touch input — LVGL 9 API
 * --------------------------------------------------------------- */
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    esp_lcd_touch_handle_t tp = (esp_lcd_touch_handle_t)lv_indev_get_user_data(indev);
    assert(tp);

    uint16_t touchpad_x;
    uint16_t touchpad_y;
    uint8_t touchpad_cnt = 0;

    esp_lcd_touch_read_data(tp);
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(tp, &touchpad_x, &touchpad_y, NULL, &touchpad_cnt, 1);
    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x;
        data->point.y = touchpad_y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static lv_indev_t *indev_init(esp_lcd_touch_handle_t tp)
{
    assert(tp);
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read);
    lv_indev_set_user_data(indev, tp);
    return indev;
}

/* ---------------------------------------------------------------
 * Tick timer
 * --------------------------------------------------------------- */
static void tick_increment(void *arg)
{
    lv_tick_inc(LVGL_PORT_TICK_PERIOD_MS);
}

static esp_err_t tick_init(void)
{
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &tick_increment,
        .name = "LVGL tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    return esp_timer_start_periodic(lvgl_tick_timer, LVGL_PORT_TICK_PERIOD_MS * 1000);
}

/* ---------------------------------------------------------------
 * LVGL task
 * --------------------------------------------------------------- */
static void lvgl_port_task(void *arg)
{
    while (!lvgl_task_running) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint32_t task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
    while (1) {
        if (lvgl_port_lock(-1)) {
            task_delay_ms = lv_timer_handler();
            lvgl_port_unlock();
        }
        if (task_delay_ms > LVGL_PORT_TASK_MAX_DELAY_MS)
            task_delay_ms = LVGL_PORT_TASK_MAX_DELAY_MS;
        else if (task_delay_ms < LVGL_PORT_TASK_MIN_DELAY_MS)
            task_delay_ms = LVGL_PORT_TASK_MIN_DELAY_MS;
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

/* ---------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------- */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t lcd_handle, esp_lcd_touch_handle_t tp_handle)
{
    lv_init();
    ESP_ERROR_CHECK(tick_init());

    lv_display_t *disp = display_init(lcd_handle);
    assert(disp);

    if (tp_handle) {
        lv_indev_t *indev = indev_init(tp_handle);
        assert(indev);
    }

    lvgl_mux = xSemaphoreCreateRecursiveMutex();
    assert(lvgl_mux);

    ESP_LOGI(TAG, "Creating LVGL task");
    BaseType_t core_id = (LVGL_PORT_TASK_CORE < 0) ? tskNO_AFFINITY : LVGL_PORT_TASK_CORE;
    BaseType_t ret = xTaskCreatePinnedToCore(lvgl_port_task, "lvgl", LVGL_PORT_TASK_STACK_SIZE, NULL,
                                             LVGL_PORT_TASK_PRIORITY, &lvgl_task_handle, core_id);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LVGL task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool lvgl_port_lock(int timeout_ms)
{
    assert(lvgl_mux && "lvgl_port_init must be called first");
    const TickType_t timeout_ticks = (timeout_ms < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(lvgl_mux, timeout_ticks) == pdTRUE;
}

void lvgl_port_unlock(void)
{
    assert(lvgl_mux && "lvgl_port_init must be called first");
    xSemaphoreGiveRecursive(lvgl_mux);
}

bool lvgl_port_notify_rgb_vsync(void)
{
    BaseType_t need_yield = pdFALSE;
#if LVGL_PORT_AVOID_TEAR_ENABLE
    xTaskNotifyFromISR(lvgl_task_handle, ULONG_MAX, eNoAction, &need_yield);
#endif
    return (need_yield == pdTRUE);
}

void lvgl_port_task_start(void)
{
    lvgl_task_running = true;
}
