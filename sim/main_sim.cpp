// LVGL SDL2 simulator for homedash UI development
// Build: make -C sim
// Run:   ./sim/homedash_sim

#include "SDL.h"
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "lvgl.h"
#include "bridge.h"

// Forward declarations from the real UI code
void ui_dashboard_create(void);
void ui_dashboard_update_time(void);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);

// --- Stubs for hardware-dependent code ---
void request_calendar_date(int, int, int) {}
void request_light_toggle(const char *id) {
    printf("[SIM] Toggle light: %s\n", id);
}

// --- Fetch real data from bridge API via curl ---
#include "cJSON.h"

#define BRIDGE_API_URL "https://esp32-bridge.dzarlax.dev/api/dashboard?key=mEleZGYkFf1W0X0kUJ5YZt0vhWT6NveFcOD2oZVX"

static bridge_data_t s_sim_data = {};

static char *exec_curl(const char *url) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "curl -sk '%s' 2>/dev/null", url);
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    size_t cap = 8192, len = 0;
    char *buf = (char *)malloc(cap);
    while (!feof(fp)) {
        len += fread(buf + len, 1, cap - len - 1, fp);
        if (len >= cap - 1) { cap *= 2; buf = (char *)realloc(buf, cap); }
    }
    pclose(fp);
    buf[len] = '\0';
    return buf;
}

static void parse_json_health(cJSON *obj) {
    if (!obj || !cJSON_IsObject(obj)) { s_sim_data.health.valid = false; return; }
    s_sim_data.health.steps      = cJSON_GetObjectItem(obj, "steps")      ? cJSON_GetObjectItem(obj, "steps")->valueint : 0;
    s_sim_data.health.steps_prev = cJSON_GetObjectItem(obj, "steps_prev") ? cJSON_GetObjectItem(obj, "steps_prev")->valueint : 0;
    s_sim_data.health.cal        = cJSON_GetObjectItem(obj, "cal")        ? cJSON_GetObjectItem(obj, "cal")->valueint : 0;
    s_sim_data.health.cal_prev   = cJSON_GetObjectItem(obj, "cal_prev")   ? cJSON_GetObjectItem(obj, "cal_prev")->valueint : 0;
    s_sim_data.health.sleep      = cJSON_GetObjectItem(obj, "sleep")      ? (float)cJSON_GetObjectItem(obj, "sleep")->valuedouble : 0;
    s_sim_data.health.sleep_prev = cJSON_GetObjectItem(obj, "sleep_prev") ? (float)cJSON_GetObjectItem(obj, "sleep_prev")->valuedouble : 0;
    s_sim_data.health.hr         = cJSON_GetObjectItem(obj, "hr")         ? cJSON_GetObjectItem(obj, "hr")->valueint : 0;
    s_sim_data.health.rhr        = cJSON_GetObjectItem(obj, "rhr")        ? cJSON_GetObjectItem(obj, "rhr")->valueint : 0;
    s_sim_data.health.hrv        = cJSON_GetObjectItem(obj, "hrv")        ? cJSON_GetObjectItem(obj, "hrv")->valueint : 0;
    s_sim_data.health.spo2       = cJSON_GetObjectItem(obj, "spo2")       ? cJSON_GetObjectItem(obj, "spo2")->valueint : 0;
    s_sim_data.health.readiness  = cJSON_GetObjectItem(obj, "readiness")  ? cJSON_GetObjectItem(obj, "readiness")->valueint : 0;
    s_sim_data.health.valid = true;
}

static void fetch_bridge_data() {
    char *json_str = exec_curl(BRIDGE_API_URL);
    if (!json_str) { printf("[SIM] curl failed\n"); return; }

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (!root) { printf("[SIM] JSON parse failed\n"); return; }

    // Health
    parse_json_health(cJSON_GetObjectItem(root, "health"));

    // Tasks
    cJSON *tasks = cJSON_GetObjectItem(root, "tasks");
    s_sim_data.task_count = 0; s_sim_data.tasks_valid = false;
    if (tasks && cJSON_IsArray(tasks)) {
        int n = cJSON_GetArraySize(tasks);
        if (n > BRIDGE_MAX_TASKS) n = BRIDGE_MAX_TASKS;
        for (int i = 0; i < n; i++) {
            cJSON *t = cJSON_GetArrayItem(tasks, i);
            bridge_task_t *bt = &s_sim_data.tasks[s_sim_data.task_count];
            memset(bt, 0, sizeof(*bt));
            cJSON *title = cJSON_GetObjectItem(t, "t");
            if (title && cJSON_IsString(title)) strncpy(bt->title, title->valuestring, 79);
            cJSON *p = cJSON_GetObjectItem(t, "p");
            if (p) bt->priority = p->valueint;
            cJSON *d = cJSON_GetObjectItem(t, "due");
            if (d && cJSON_IsString(d)) strncpy(bt->due, d->valuestring, 11);
            s_sim_data.task_count++;
        }
        s_sim_data.tasks_valid = true;
    }

    // News
    cJSON *news = cJSON_GetObjectItem(root, "news");
    s_sim_data.news_count = 0; s_sim_data.news_valid = false;
    if (news && cJSON_IsArray(news)) {
        int n = cJSON_GetArraySize(news);
        if (n > BRIDGE_MAX_NEWS) n = BRIDGE_MAX_NEWS;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(news, i);
            bridge_news_t *bn = &s_sim_data.news[s_sim_data.news_count];
            memset(bn, 0, sizeof(*bn));
            cJSON *t = cJSON_GetObjectItem(item, "t");
            if (t && cJSON_IsString(t)) strncpy(bn->title, t->valuestring, 119);
            cJSON *c = cJSON_GetObjectItem(item, "c");
            if (c && cJSON_IsString(c)) strncpy(bn->category, c->valuestring, 23);
            cJSON *h = cJSON_GetObjectItem(item, "h");
            if (h) bn->hours_ago = h->valueint;
            s_sim_data.news_count++;
        }
        s_sim_data.news_valid = true;
    }

    // Lights
    cJSON *lights = cJSON_GetObjectItem(root, "lights");
    s_sim_data.light_count = 0; s_sim_data.lights_valid = false;
    if (lights && cJSON_IsArray(lights)) {
        int n = cJSON_GetArraySize(lights);
        if (n > BRIDGE_MAX_LIGHTS) n = BRIDGE_MAX_LIGHTS;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(lights, i);
            bridge_light_t *bl = &s_sim_data.lights[s_sim_data.light_count];
            memset(bl, 0, sizeof(*bl));
            cJSON *id = cJSON_GetObjectItem(item, "id");
            if (id && cJSON_IsString(id)) strncpy(bl->entity_id, id->valuestring, 47);
            cJSON *nm = cJSON_GetObjectItem(item, "n");
            if (nm && cJSON_IsString(nm)) strncpy(bl->name, nm->valuestring, 39);
            cJSON *on = cJSON_GetObjectItem(item, "on");
            if (on) bl->on = cJSON_IsTrue(on);
            cJSON *br = cJSON_GetObjectItem(item, "br");
            if (br) bl->brightness = br->valueint;
            s_sim_data.light_count++;
        }
        s_sim_data.lights_valid = true;
    }

    // Sensors
    cJSON *sensors = cJSON_GetObjectItem(root, "sensors");
    s_sim_data.sensor_count = 0; s_sim_data.sensors_valid = false;
    if (sensors && cJSON_IsArray(sensors)) {
        int n = cJSON_GetArraySize(sensors);
        if (n > BRIDGE_MAX_SENSORS) n = BRIDGE_MAX_SENSORS;
        for (int i = 0; i < n; i++) {
            cJSON *item = cJSON_GetArrayItem(sensors, i);
            bridge_sensor_t *bs = &s_sim_data.sensors[s_sim_data.sensor_count];
            memset(bs, 0, sizeof(*bs));
            cJSON *nm = cJSON_GetObjectItem(item, "n");
            if (nm && cJSON_IsString(nm)) strncpy(bs->name, nm->valuestring, 39);
            cJSON *v = cJSON_GetObjectItem(item, "v");
            if (v && cJSON_IsString(v)) strncpy(bs->value, v->valuestring, 15);
            cJSON *u = cJSON_GetObjectItem(item, "u");
            if (u && cJSON_IsString(u)) strncpy(bs->unit, u->valuestring, 7);
            s_sim_data.sensor_count++;
        }
        s_sim_data.sensors_valid = true;
    }

    cJSON_Delete(root);
    printf("[SIM] Fetched: health=%d tasks=%d news=%d lights=%d sensors=%d\n",
           s_sim_data.health.valid, s_sim_data.task_count,
           s_sim_data.news_count, s_sim_data.light_count, s_sim_data.sensor_count);
}

// --- SDL + LVGL glue ---
#define DISP_W 1024
#define DISP_H 600

static SDL_Window   *sdl_win = NULL;
static SDL_Renderer *sdl_ren = NULL;
static SDL_Texture  *sdl_tex = NULL;
static lv_color_t    fb[DISP_W * DISP_H];

static void sdl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(&fb[y * DISP_W + area->x1], color_p, (area->x2 - area->x1 + 1) * sizeof(lv_color_t));
        color_p += (area->x2 - area->x1 + 1);
    }
    lv_disp_flush_ready(drv);
}

static void sdl_mouse_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    int x, y;
    uint32_t buttons = SDL_GetMouseState(&x, &y);
    data->point.x = x;
    data->point.y = y;
    data->state = (buttons & SDL_BUTTON(1)) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int main()
{
    SDL_Init(SDL_INIT_VIDEO);
    sdl_win = SDL_CreateWindow("HomeDash Simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISP_W, DISP_H, 0);
    sdl_ren = SDL_CreateRenderer(sdl_win, -1, SDL_RENDERER_ACCELERATED);
    sdl_tex = SDL_CreateTexture(sdl_ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, DISP_W, DISP_H);

    lv_init();

    // Display driver
    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[DISP_W * 60];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, DISP_W * 60);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_W;
    disp_drv.ver_res = DISP_H;
    disp_drv.flush_cb = sdl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Mouse input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = sdl_mouse_read;
    lv_indev_drv_register(&indev_drv);

    // Create UI
    ui_dashboard_create();

    // Fetch real data from bridge API
    fetch_bridge_data();
    ui_dashboard_update_bridge(&s_sim_data);
    ui_dashboard_update_ha(&s_sim_data);

    // Main loop — refresh data every 30s
    bool running = true;
    uint32_t last_fetch = SDL_GetTicks();
    while (running) {
        // Refresh data periodically
        if (SDL_GetTicks() - last_fetch > 30000) {
            fetch_bridge_data();
            ui_dashboard_update_bridge(&s_sim_data);
            ui_dashboard_update_ha(&s_sim_data);
            last_fetch = SDL_GetTicks();
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
        }

        lv_tick_inc(5);
        lv_timer_handler();

        // Render framebuffer to SDL
        SDL_UpdateTexture(sdl_tex, NULL, fb, DISP_W * sizeof(lv_color_t));
        SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL);
        SDL_RenderPresent(sdl_ren);

        SDL_Delay(5);
    }

    SDL_DestroyTexture(sdl_tex);
    SDL_DestroyRenderer(sdl_ren);
    SDL_DestroyWindow(sdl_win);
    SDL_Quit();
    return 0;
}
