#include "weather_icons.h"

#define ICON_SIZE 48

#define CLR_SUN     lv_color_hex(0xFFD700)
#define CLR_CLOUD   lv_color_hex(0xA0A0B0)
#define CLR_RAIN    lv_color_hex(0x4488FF)
#define CLR_SNOW    lv_color_hex(0xE0E8FF)
#define CLR_BOLT    lv_color_hex(0xFFFF00)
#define CLR_FOG     lv_color_hex(0x888899)
#define CLR_BG      lv_color_hex(0x16213E)

static void draw_sun(lv_obj_t *canvas)
{
    // Filled circle (rounded rect) in center
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = CLR_SUN;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = 10;
    rdsc.border_width = 0;
    lv_canvas_draw_rect(canvas, 14, 14, 20, 20, &rdsc);

    // 8 rays radiating outward
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = CLR_SUN;
    ldsc.width = 2;
    ldsc.opa = LV_OPA_COVER;

    static const int8_t dx[] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t dy[] = {-1, -1, 0, 1, 1, 1, 0, -1};

    for (int i = 0; i < 8; i++) {
        lv_point_t pts[2];
        pts[0].x = 24 + dx[i] * 13;
        pts[0].y = 24 + dy[i] * 13;
        pts[1].x = 24 + dx[i] * 20;
        pts[1].y = 24 + dy[i] * 20;
        lv_canvas_draw_line(canvas, pts, 2, &ldsc);
    }
}

static void draw_cloud(lv_obj_t *canvas, int x_off, int y_off)
{
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = CLR_CLOUD;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.border_width = 0;

    // Base of cloud — wide rounded rect
    rdsc.radius = 8;
    lv_canvas_draw_rect(canvas, x_off, y_off + 6, 28, 16, &rdsc);

    // Top bump — taller rounded rect overlapping
    rdsc.radius = 10;
    lv_canvas_draw_rect(canvas, x_off + 8, y_off, 20, 18, &rdsc);
}

static void draw_rain_lines(lv_obj_t *canvas, int count, bool angled)
{
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = CLR_RAIN;
    ldsc.width = 2;
    ldsc.opa = LV_OPA_COVER;

    int spacing = 36 / (count + 1);
    for (int i = 0; i < count; i++) {
        lv_point_t pts[2];
        int x = 6 + spacing * (i + 1);
        pts[0].x = x;
        pts[0].y = 30;
        pts[1].x = angled ? x - 3 : x;
        pts[1].y = 42;
        lv_canvas_draw_line(canvas, pts, 2, &ldsc);
    }
}

static void draw_snow_dots(lv_obj_t *canvas)
{
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = CLR_SNOW;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = 2;
    rdsc.border_width = 0;

    static const int positions[] = {8, 16, 24, 32, 40};
    for (int i = 0; i < 5; i++) {
        int y = (i % 2 == 0) ? 34 : 40;
        lv_canvas_draw_rect(canvas, positions[i] - 2, y, 4, 4, &rdsc);
    }
}

static void draw_lightning(lv_obj_t *canvas)
{
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = CLR_BOLT;
    ldsc.width = 2;
    ldsc.opa = LV_OPA_COVER;

    lv_point_t pts1[2] = {{24, 26}, {20, 34}};
    lv_canvas_draw_line(canvas, pts1, 2, &ldsc);
    lv_point_t pts2[2] = {{20, 34}, {28, 34}};
    lv_canvas_draw_line(canvas, pts2, 2, &ldsc);
    lv_point_t pts3[2] = {{28, 34}, {22, 46}};
    lv_canvas_draw_line(canvas, pts3, 2, &ldsc);
}

static void draw_fog_lines(lv_obj_t *canvas)
{
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = CLR_FOG;
    ldsc.width = 3;
    ldsc.opa = LV_OPA_COVER;

    for (int i = 0; i < 3; i++) {
        lv_point_t pts[2];
        pts[0].x = 6 + i * 3;
        pts[0].y = 14 + i * 12;
        pts[1].x = 42 - i * 3;
        pts[1].y = 14 + i * 12;
        lv_canvas_draw_line(canvas, pts, 2, &ldsc);
    }
}

void weather_icon_draw(lv_obj_t *canvas, int weather_code)
{
    lv_canvas_fill_bg(canvas, CLR_BG, LV_OPA_COVER);

    if (weather_code == 0) {
        draw_sun(canvas);
    } else if (weather_code >= 1 && weather_code <= 3) {
        draw_cloud(canvas, 10, 8);
    } else if (weather_code == 45 || weather_code == 48) {
        draw_fog_lines(canvas);
    } else if (weather_code >= 51 && weather_code <= 55) {
        draw_cloud(canvas, 10, 4);
        draw_rain_lines(canvas, 3, false);
    } else if (weather_code >= 61 && weather_code <= 65) {
        draw_cloud(canvas, 10, 4);
        draw_rain_lines(canvas, 5, false);
    } else if (weather_code >= 71 && weather_code <= 75) {
        draw_cloud(canvas, 10, 4);
        draw_snow_dots(canvas);
    } else if (weather_code >= 80 && weather_code <= 82) {
        draw_cloud(canvas, 10, 4);
        draw_rain_lines(canvas, 5, true);
    } else if (weather_code >= 95 && weather_code <= 99) {
        draw_cloud(canvas, 10, 2);
        draw_lightning(canvas);
    } else {
        draw_cloud(canvas, 10, 8);
    }
}
