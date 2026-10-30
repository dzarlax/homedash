// LVGL SDL2 simulator for HomeDash UI development on macOS/Linux.
// Build: make -C sim
// Run:   ./sim/homedash_sim

#include <SDL.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "lvgl.h"
#include "bridge.h"
#include "photo_frame.h"
#include "web_settings.h"
#include "ui_dashboard.h"
#include "ui_strings.h"
#include <cstdlib>

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
extern "C" void sim_set_locale_test_fixtures(void);
extern "C" {
LV_FONT_DECLARE(font_montserrat_16_cyr);
LV_FONT_DECLARE(font_montserrat_24_cyr);
}

static SDL_Renderer *s_renderer = nullptr;
static SDL_Texture *s_texture = nullptr;
static int s_pointer_x = 0;
static int s_pointer_y = 0;
static bool s_pointer_down = false;
static bool s_photo_fixture = false;

static bool label_contains(lv_obj_t *obj, const char *needle)
{
    if (lv_obj_check_type(obj, &lv_label_class) &&
        std::strcmp(lv_label_get_text(obj), needle) == 0) return true;
    const uint32_t count = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < count; ++i)
        if (label_contains(lv_obj_get_child(obj, static_cast<int32_t>(i)), needle)) return true;
    return false;
}

static bool glyphs_available(const lv_font_t *font)
{
    static const uint32_t glyphs[] = {0x010D, 0x0107, 0x0111, 0x0161, 0x017E,
                                      0x010C, 0x0106, 0x0110, 0x0160, 0x017D};
    for (uint32_t cp : glyphs) {
        lv_font_glyph_dsc_t dsc = {};
        if (!lv_font_get_glyph_dsc(font, &dsc, cp, 0) || dsc.box_w == 0 || dsc.box_h == 0) return false;
    }
    return true;
}

static bool save_screenshot(const char *path)
{
    if (!path || !path[0]) return true;
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, DISP_W, DISP_H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;
    const int result = SDL_RenderReadPixels(s_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                                            surface->pixels, surface->pitch);
    const bool saved = result == 0 && SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    return saved;
}

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
    if (s_photo_fixture) photo_frame_filter_touch(s_pointer_down, s_pointer_x, s_pointer_y, data);
    else { data->point.x=s_pointer_x; data->point.y=s_pointer_y; data->state=s_pointer_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED; }
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
    if (!s_renderer) s_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
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
    const bool locale_test = std::getenv("HOMEDASH_SIM_LOCALE_TEST") != nullptr;
    if (locale_test) sim_set_locale_test_fixtures();
    ui_dashboard_create();
    s_photo_fixture = std::getenv("HOMEDASH_SIM_PHOTO_FRAME") != nullptr;
    if (s_photo_fixture) { photo_frame_sim_accelerate(); photo_frame_ui_init(); photo_frame_network_init("fixture", "fixture"); }
    refresh_all_data();

    if (locale_test) {
        static const char *locales[] = {"ru", "en", "sr-Latn", "ru"};
        ui_dashboard_sim_set_page(1);
        for (const char *locale : locales) {
            web_preferences_t prefs = {};
            web_preferences_defaults(&prefs);
            prefs.revision = 1;
            std::snprintf(prefs.settings_url, sizeof(prefs.settings_url), "%s", "http://homedash.local");
            std::snprintf(prefs.locale, sizeof(prefs.locale), "%s", locale);
            ui_dashboard_apply_preferences(&prefs);
            for (int i = 0; i < 4; ++i) lv_timer_handler();
            if (ui_dashboard_sim_current_page() != 1 || !ui_dashboard_sim_fixed_labels_fit()) {
                std::fprintf(stderr, "[SIM] Locale switch changed page state or clipped a fixed label (%s)\n", locale);
                return 6;
            }
            if (!label_contains(lv_screen_active(), "Today") || !label_contains(lv_screen_active(), "Дом")) {
                std::fprintf(stderr, "[SIM] Locale %s translated provider content\n", locale);
                return 2;
            }
            const char *expected_header = std::strcmp(locale, "en") == 0 ? "Tasks" :
                                          std::strcmp(locale, "sr-Latn") == 0 ? "Zadaci" : "Задачи";
            if (!label_contains(lv_screen_active(), expected_header)) {
                std::fprintf(stderr, "[SIM] Locale %s did not update static page labels (active=%s expected=%s)\n",
                             locale, ui_strings_locale(), ui_text("Задачи"));
                return 3;
            }
        }
        if (!glyphs_available(&font_montserrat_16_cyr) || !glyphs_available(&font_montserrat_24_cyr)) {
            std::fprintf(stderr, "[SIM] Serbian Latin glyph lookup failed\n");
            return 4;
        }
        ui_dashboard_sim_open_reader();
        const int32_t reader_scroll = ui_dashboard_sim_reader_scroll_y();
        static const char *reader_locales[] = {"en", "sr-Latn", "ru"};
        static const char *reader_due[] = {"Due: 2030-01-01", "Rok: 2030-01-01", "Срок: 2030-01-01"};
        for (int i = 0; i < 3; ++i) {
            web_preferences_t prefs = {};
            web_preferences_defaults(&prefs);
            prefs.revision = 1;
            std::snprintf(prefs.settings_url, sizeof(prefs.settings_url), "%s", "http://homedash.local");
            std::snprintf(prefs.locale, sizeof(prefs.locale), "%s", reader_locales[i]);
            ui_dashboard_apply_preferences(&prefs);
            for (int j = 0; j < 4; ++j) lv_timer_handler();
            if (!ui_dashboard_reader_open() || !label_contains(lv_screen_active(), "Today") ||
                !label_contains(lv_screen_active(), reader_due[i]) ||
                ui_dashboard_sim_reader_scroll_y() != reader_scroll) {
                std::fprintf(stderr, "[SIM] Open reader lost user content, translated meta, or scroll on %s\n",
                             reader_locales[i]);
                return 7;
            }
        }
        web_preferences_t reset_prefs = {};
        web_preferences_defaults(&reset_prefs);
        reset_prefs.revision = 1;
        std::snprintf(reset_prefs.settings_url, sizeof(reset_prefs.settings_url), "%s", "http://homedash.local");
        std::snprintf(reset_prefs.locale, sizeof(reset_prefs.locale), "%s", "en");
        ui_dashboard_apply_preferences(&reset_prefs);
        if (!label_contains(lv_screen_active(), "Due: 2030-01-01")) return 8;
        ui_dashboard_reset_preferences();
        if (std::strcmp(ui_strings_locale(), "ru") ||
            !label_contains(lv_screen_active(), "Срок: 2030-01-01") ||
            !ui_dashboard_reader_open()) {
            std::fprintf(stderr, "[SIM] Reset did not restore Russian dynamic reader labels\n");
            return 8;
        }
        ui_dashboard_sim_close_reader();
        const char *shot_locale = std::getenv("HOMEDASH_SIM_SCREENSHOT_LOCALE");
        const char *shot_path = std::getenv("HOMEDASH_SIM_SCREENSHOT_PATH");
        const char *page_env = std::getenv("HOMEDASH_SIM_SCREENSHOT_PAGE");
        if (shot_locale && shot_path) {
            web_preferences_t prefs = {};
            web_preferences_defaults(&prefs);
            prefs.revision = 1;
            std::snprintf(prefs.settings_url, sizeof(prefs.settings_url), "%s", "http://homedash.local");
            std::snprintf(prefs.locale, sizeof(prefs.locale), "%s", shot_locale);
            ui_dashboard_apply_preferences(&prefs);
            ui_dashboard_sim_set_page(page_env ? std::atoi(page_env) : 0);
            for (int i = 0; i < 5; ++i) lv_timer_handler();
            lv_refr_now(display);
            render();
            if (!save_screenshot(shot_path)) {
                std::fprintf(stderr, "[SIM] Screenshot save failed: %s\n", SDL_GetError());
                return 5;
            }
        }
        if (std::getenv("HOMEDASH_SIM_PHOTO_MODE_TEST")) {
            bool started = false;
            for (int i = 0; i < 100 && !started; ++i) {
                started = photo_frame_start_now();
                if (!started) SDL_Delay(10);
            }
            if (!started) {
                std::fprintf(stderr, "[SIM] Could not start photo frame fixture\n");
                return 8;
            }
            for (int i = 0; i < 500 && !photo_frame_sim_visible(); ++i) {
                lv_tick_inc(20);
                lv_timer_handler();
                SDL_Delay(10);
            }
            if (!photo_frame_sim_visible()) {
                std::fprintf(stderr, "[SIM] Photo frame fixture did not open\n");
                return 9;
            }
            static const char *photo_locales[] = {"en", "sr-Latn", "ru"};
            for (const char *locale : photo_locales) {
                web_preferences_t prefs = {};
                web_preferences_defaults(&prefs);
                prefs.revision = 1;
                std::snprintf(prefs.settings_url, sizeof(prefs.settings_url), "%s", "http://homedash.local");
                std::snprintf(prefs.locale, sizeof(prefs.locale), "%s", locale);
                ui_dashboard_apply_preferences(&prefs);
                if (!photo_frame_sim_visible()) {
                    std::fprintf(stderr, "[SIM] Locale switch exited photo mode (%s)\n", locale);
                    return 10;
                }
            }
            std::puts("[SIM] Photo frame remains open through locale changes");
        }
        std::puts("[SIM] Locale cycle, provider data preservation, static labels, and Serbian glyphs OK");
        SDL_DestroyTexture(s_texture);
        SDL_DestroyRenderer(s_renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

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
