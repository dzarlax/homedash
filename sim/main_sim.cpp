// LVGL Win32 simulator for HomeDash UI development.
// Build: powershell -ExecutionPolicy Bypass -File sim/build_windows.ps1
// Run:   .\sim\build\windows\homedash_sim.exe

#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "bridge.h"

void ui_dashboard_create(void);
void ui_dashboard_update_time(void);
void ui_dashboard_update_weather(const bridge_weather_t *data);
void ui_dashboard_update_ha_calendar(const bridge_cal_data_t *data);
void ui_dashboard_update_transport(const bridge_transport_t *data);
void ui_dashboard_update_bridge(const bridge_data_t *data);
void ui_dashboard_update_ha(const bridge_data_t *data);

extern "C" void sim_init_fixtures(void);

static constexpr int DISP_W = 1024;
static constexpr int DISP_H = 600;

static HWND s_hwnd = NULL;
static uint16_t s_fb[DISP_W * DISP_H];
static uint32_t s_bgra[DISP_W * DISP_H];
static int s_mouse_x = 0;
static int s_mouse_y = 0;
static bool s_mouse_down = false;

static uint32_t rgb565_to_bgra(uint16_t px)
{
    uint32_t r = (px >> 11) & 0x1f;
    uint32_t g = (px >> 5) & 0x3f;
    uint32_t b = px & 0x1f;

    r = (r * 255) / 31;
    g = (g * 255) / 63;
    b = (b * 255) / 31;
    return (r << 16) | (g << 8) | b;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t width = area->x2 - area->x1 + 1;
    const uint16_t *src = (const uint16_t *)px_map;

    for (int32_t y = area->y1; y <= area->y2; y++) {
        memcpy(&s_fb[y * DISP_W + area->x1], src, width * sizeof(uint16_t));
        src += width;
    }

    if (s_hwnd) {
        RECT rect = {(LONG)area->x1, (LONG)area->y1, (LONG)area->x2 + 1, (LONG)area->y2 + 1};
        InvalidateRect(s_hwnd, &rect, FALSE);
    }

    lv_display_flush_ready(disp);
}

static void pointer_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point.x = s_mouse_x;
    data->point.y = s_mouse_y;
    data->state = s_mouse_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void paint_window(HWND hwnd)
{
    for (int i = 0; i < DISP_W * DISP_H; i++) {
        s_bgra[i] = rgb565_to_bgra(s_fb[i]);
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = DISP_W;
    bmi.bmiHeader.biHeight = -DISP_H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    SetDIBitsToDevice(hdc, 0, 0, DISP_W, DISP_H, 0, 0, 0, DISP_H, s_bgra, &bmi, DIB_RGB_COLORS);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_PAINT:
        paint_window(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        s_mouse_x = GET_X_LPARAM(lparam);
        s_mouse_y = GET_Y_LPARAM(lparam);
        return 0;
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        s_mouse_down = true;
        s_mouse_x = GET_X_LPARAM(lparam);
        s_mouse_y = GET_Y_LPARAM(lparam);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        s_mouse_down = false;
        s_mouse_x = GET_X_LPARAM(lparam);
        s_mouse_y = GET_Y_LPARAM(lparam);
        return 0;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

static bool create_window(HINSTANCE instance)
{
    const char *class_name = "HomeDashSimulatorWindow";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = instance;
    wc.lpszClassName = class_name;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

    if (!RegisterClassA(&wc)) {
        return false;
    }

    RECT rc = {0, 0, DISP_W, DISP_H};
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    s_hwnd = CreateWindowExA(
        0, class_name, "HomeDash Simulator",
        WS_OVERLAPPEDWINDOW,
        20, 20,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, instance, NULL);

    if (!s_hwnd) {
        return false;
    }

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
    return true;
}

static void refresh_all_data(void)
{
    const bridge_data_t *data = bridge_get_data();
    const bridge_cal_data_t *calendar = bridge_get_calendar_data();

    ui_dashboard_update_time();
    ui_dashboard_update_weather(&data->weather);
    ui_dashboard_update_ha_calendar(calendar);
    ui_dashboard_update_transport(&data->transport);
    ui_dashboard_update_bridge(data);
    ui_dashboard_update_ha(data);
}

int main(void)
{
    HINSTANCE instance = GetModuleHandleW(NULL);
    if (!create_window(instance)) {
        fprintf(stderr, "[SIM] Failed to create Win32 window\n");
        return 1;
    }

    lv_init();

    lv_display_t *display = lv_display_create(DISP_W, DISP_H);
    if (!display) {
        fprintf(stderr, "[SIM] Failed to create LVGL display\n");
        return 1;
    }

    static uint16_t draw_buf_1[DISP_W * 80];
    static uint16_t draw_buf_2[DISP_W * 80];
    lv_display_set_flush_cb(display, flush_cb);
    lv_display_set_buffers(display, draw_buf_1, draw_buf_2, sizeof(draw_buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *pointer = lv_indev_create();
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, pointer_read_cb);

    sim_init_fixtures();
    ui_dashboard_create();
    refresh_all_data();

    MSG msg;
    bool running = true;
    DWORD last_data_refresh = GetTickCount();
    while (running) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DWORD now = GetTickCount();
        if (now - last_data_refresh >= 30000) {
            refresh_all_data();
            last_data_refresh = now;
        }

        lv_tick_inc(5);
        lv_timer_handler();
        Sleep(5);
    }

    return 0;
}
