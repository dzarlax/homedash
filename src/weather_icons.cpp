#include "weather_icons.h"

#define ICON_SIZE 48

#define CLR_SUN     lv_color_hex(0xFFD700)
#define CLR_CLOUD   lv_color_hex(0xA0A0B0)
#define CLR_RAIN    lv_color_hex(0x4488FF)
#define CLR_SNOW    lv_color_hex(0xE0E8FF)
#define CLR_BOLT    lv_color_hex(0xFFFF00)
#define CLR_FOG     lv_color_hex(0x888899)
#define CLR_BG      lv_color_hex(0x16213E)

/* Helper: draw filled rect on layer */
static inline void layer_rect(lv_layer_t *layer, int x, int y, int w, int h,
                               lv_color_t color, int32_t radius)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color    = color;
    dsc.bg_opa      = LV_OPA_COVER;
    dsc.radius      = radius;
    dsc.border_width = 0;
    lv_area_t area = {(int32_t)x, (int32_t)y, (int32_t)(x + w - 1), (int32_t)(y + h - 1)};
    lv_draw_rect(layer, &dsc, &area);
}

/* Helper: draw line on layer */
static inline void layer_line(lv_layer_t *layer, int x1, int y1, int x2, int y2,
                               lv_color_t color, int32_t width)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = color;
    dsc.width = width;
    dsc.opa   = LV_OPA_COVER;
    dsc.p1.x  = x1;
    dsc.p1.y  = y1;
    dsc.p2.x  = x2;
    dsc.p2.y  = y2;
    lv_draw_line(layer, &dsc);
}

static void draw_sun(lv_layer_t *layer)
{
    layer_rect(layer, 14, 14, 20, 20, CLR_SUN, 10);

    static const int8_t dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int i = 0; i < 8; i++) {
        layer_line(layer,
                   24 + dx[i] * 13, 24 + dy[i] * 13,
                   24 + dx[i] * 20, 24 + dy[i] * 20,
                   CLR_SUN, 2);
    }
}

static void draw_cloud(lv_layer_t *layer, int x_off, int y_off)
{
    layer_rect(layer, x_off,     y_off + 6, 28, 16, CLR_CLOUD, 8);
    layer_rect(layer, x_off + 8, y_off,     20, 18, CLR_CLOUD, 10);
}

static void draw_rain_lines(lv_layer_t *layer, int count, bool angled)
{
    int spacing = 36 / (count + 1);
    for (int i = 0; i < count; i++) {
        int x = 6 + spacing * (i + 1);
        layer_line(layer, x, 30, angled ? x - 3 : x, 42, CLR_RAIN, 2);
    }
}

static void draw_snow_dots(lv_layer_t *layer)
{
    static const int positions[] = {8, 16, 24, 32, 40};
    for (int i = 0; i < 5; i++) {
        int y = (i % 2 == 0) ? 34 : 40;
        layer_rect(layer, positions[i] - 2, y, 4, 4, CLR_SNOW, 2);
    }
}

static void draw_lightning(lv_layer_t *layer)
{
    layer_line(layer, 24, 26, 20, 34, CLR_BOLT, 2);
    layer_line(layer, 20, 34, 28, 34, CLR_BOLT, 2);
    layer_line(layer, 28, 34, 22, 46, CLR_BOLT, 2);
}

static void draw_fog_lines(lv_layer_t *layer)
{
    for (int i = 0; i < 3; i++) {
        layer_line(layer,
                   6 + i * 3,  14 + i * 12,
                   42 - i * 3, 14 + i * 12,
                   CLR_FOG, 3);
    }
}

void weather_icon_draw(lv_obj_t *canvas, int weather_code)
{
    lv_canvas_fill_bg(canvas, CLR_BG, LV_OPA_COVER);

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    if (weather_code == 0) {
        draw_sun(&layer);
    } else if (weather_code >= 1 && weather_code <= 3) {
        draw_cloud(&layer, 10, 8);
    } else if (weather_code == 45 || weather_code == 48) {
        draw_fog_lines(&layer);
    } else if (weather_code >= 51 && weather_code <= 55) {
        draw_cloud(&layer, 10, 4);
        draw_rain_lines(&layer, 3, false);
    } else if (weather_code >= 61 && weather_code <= 65) {
        draw_cloud(&layer, 10, 4);
        draw_rain_lines(&layer, 5, false);
    } else if (weather_code >= 71 && weather_code <= 75) {
        draw_cloud(&layer, 10, 4);
        draw_snow_dots(&layer);
    } else if (weather_code >= 80 && weather_code <= 82) {
        draw_cloud(&layer, 10, 4);
        draw_rain_lines(&layer, 5, true);
    } else if (weather_code >= 95 && weather_code <= 99) {
        draw_cloud(&layer, 10, 2);
        draw_lightning(&layer);
    } else {
        draw_cloud(&layer, 10, 8);
    }

    lv_canvas_finish_layer(canvas, &layer);
}
