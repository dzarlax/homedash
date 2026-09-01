// LVGL SDL2 simulator for HomeDash UI development on macOS/Linux.
// Build: make -C sim
// Run:   ./sim/homedash_sim

#include <SDL.h>
#include <cstdint>
#include <cstdio>

#include "lvgl.h"
#include "bridge.h"

static constexpr int DISP_W = 1024;
static constexpr int DISP_H = 600;

void ui_dashboard_create(void);
void ui_dashboard_update_time(void);
void ui_dashboard_update_weather(const bridge_weather_t *data);
void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data);
void ui_dashboard_update_transport(const bridge_transport_t *data);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);
extern "C" void sim_init_fixtures(void);

static SDL_Renderer *s_renderer = nullptr;
static SDL_Texture *s_texture = nullptr;
static int s_pointer_x = 0;
static int s_pointer_y = 0;
static bool s_pointer_down = false;

static void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    SDL_Rect dirty = {
        .x = area->x1,
        .y = area->y1,
        .w = area->x2 - area->x1 + 1,
        .h = area->y2 - area->y1 + 1,
    };
    SDL_UpdateTexture(s_texture, &dirty, pixels, dirty.w * static_cast<int>(sizeof(uint16_t)));
    lv_display_flush_ready(display);
}

static void pointer_read_cb(lv_indev_t *, lv_indev_data_t *data)
{
    data->point.x = s_pointer_x;
    data->point.y = s_pointer_y;
    data->state = s_pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void refresh_all_data(void)
{
    bridge_data_t data = {};
    bridge_cal_data_t calendar = {};
    if (!bridge_copy_data(&data) || !bridge_copy_calendar_data(&calendar)) return;

    ui_dashboard_update_time();
    ui_dashboard_update_weather(&data.weather);
    ui_dashboard_update_ha_calendar(&calendar);
    ui_dashboard_update_transport(&data.transport);
    ui_dashboard_update_bridge(&data);
    ui_dashboard_update_ha(&data);
}

static void render(void)
{
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, nullptr, nullptr);
    SDL_RenderPresent(s_renderer);
}

int main()
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[SIM] SDL initialization failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("HomeDash Simulator", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, DISP_W, DISP_H,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "[SIM] Window creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    s_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    s_texture = s_renderer ? SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                                               SDL_TEXTUREACCESS_STREAMING, DISP_W, DISP_H)
                           : nullptr;
    if (!s_texture) {
        std::fprintf(stderr, "[SIM] Renderer or texture creation failed: %s\n", SDL_GetError());
        if (s_renderer) SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    lv_init();
    lv_display_t *display = lv_display_create(DISP_W, DISP_H);
    static uint16_t draw_buf_1[DISP_W * 80];
    static uint16_t draw_buf_2[DISP_W * 80];
    lv_display_set_buffers(display, draw_buf_1, draw_buf_2, sizeof(draw_buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_cb);

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read_cb);

    sim_init_fixtures();
    ui_dashboard_create();
    refresh_all_data();

    bool running = true;
    uint32_t last_refresh = SDL_GetTicks();
    uint32_t last_lvgl_tick = last_refresh;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
                int window_w = DISP_W;
                int window_h = DISP_H;
                SDL_GetWindowSize(window, &window_w, &window_h);
                const int x = event.type == SDL_MOUSEMOTION ? event.motion.x : event.button.x;
                const int y = event.type == SDL_MOUSEMOTION ? event.motion.y : event.button.y;
                s_pointer_x = x * DISP_W / window_w;
                s_pointer_y = y * DISP_H / window_h;
                if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) s_pointer_down = true;
                if (event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) s_pointer_down = false;
            }
        }

        const uint32_t now = SDL_GetTicks();
        lv_tick_inc(now - last_lvgl_tick);
        last_lvgl_tick = now;
        if (now - last_refresh >= 30000) {
            refresh_all_data();
            last_refresh = now;
        }
        lv_timer_handler();
        render();
        SDL_Delay(5);
    }

    SDL_DestroyTexture(s_texture);
    SDL_DestroyRenderer(s_renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
