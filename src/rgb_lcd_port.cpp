#include "rgb_lcd_port.h"
#include "lvgl_port.h"

static const char *TAG_LCD = "rgb_lcd_port";
static esp_lcd_panel_handle_t panel_handle = NULL;

// Vsync event callback - notifies LVGL that frame transmission is complete
IRAM_ATTR static bool rgb_lcd_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *edata, void *user_ctx)
{
    return lvgl_port_notify_rgb_vsync();
}

esp_lcd_panel_handle_t waveshare_esp32_s3_rgb_lcd_init()
{
    ESP_LOGI(TAG_LCD, "Install RGB LCD panel driver");

    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = LCD_PIXEL_CLOCK_HZ,
            .h_res = LCD_H_RES,
            .v_res = LCD_V_RES,
            .hsync_pulse_width = 162,
            .hsync_back_porch = 152,
            .hsync_front_porch = 48,
            .vsync_pulse_width = 45,
            .vsync_back_porch = 13,
            .vsync_front_porch = 3,
            .flags = {
                .pclk_active_neg = 1,
            },
        },
        .data_width = RGB_DATA_WIDTH,
        .bits_per_pixel = RGB_BIT_PER_PIXEL,
        .num_fbs = LCD_RGB_BUFFER_NUMS,
        .bounce_buffer_size_px = RGB_BOUNCE_BUFFER_SIZE,
        .sram_trans_align = 4,
        .psram_trans_align = 64,
        .hsync_gpio_num = LCD_IO_RGB_HSYNC,
        .vsync_gpio_num = LCD_IO_RGB_VSYNC,
        .de_gpio_num = LCD_IO_RGB_DE,
        .pclk_gpio_num = LCD_IO_RGB_PCLK,
        .disp_gpio_num = LCD_IO_RGB_DISP,
        .data_gpio_nums = {
            LCD_IO_RGB_DATA0,  LCD_IO_RGB_DATA1,  LCD_IO_RGB_DATA2,
            LCD_IO_RGB_DATA3,  LCD_IO_RGB_DATA4,  LCD_IO_RGB_DATA5,
            LCD_IO_RGB_DATA6,  LCD_IO_RGB_DATA7,  LCD_IO_RGB_DATA8,
            LCD_IO_RGB_DATA9,  LCD_IO_RGB_DATA10, LCD_IO_RGB_DATA11,
            LCD_IO_RGB_DATA12, LCD_IO_RGB_DATA13, LCD_IO_RGB_DATA14,
            LCD_IO_RGB_DATA15,
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_rgb_panel(&panel_config, &panel_handle));

    // Register vsync and bounce-frame-finish callbacks (matches manufacturer reference)
    ESP_LOGI(TAG_LCD, "Register vsync callbacks");
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
#if RGB_BOUNCE_BUFFER_SIZE > 0
        .on_bounce_frame_finish = rgb_lcd_on_vsync_event,
#else
        .on_vsync = rgb_lcd_on_vsync_event,
#endif
    };
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, NULL));

    ESP_LOGI(TAG_LCD, "Initialize RGB LCD panel");
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    return panel_handle;
}

void wavesahre_rgb_lcd_bl_on()
{
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 1);
}

void wavesahre_rgb_lcd_bl_off()
{
    IO_EXTENSION_Output(IO_EXTENSION_IO_2, 0);
}

void wavesahre_rgb_lcd_display(uint8_t *Image)
{
    esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, LCD_H_RES, LCD_V_RES, Image);
}

void waveshare_get_frame_buffer(void **buf1, void **buf2)
{
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, buf1, buf2));
}
