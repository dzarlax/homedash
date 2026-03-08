# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

```bash
pio run                  # Build
pio run -t upload        # Build + flash (COM6)
pio device monitor       # Serial monitor (115200 baud)
```

Framework is **ESP-IDF 5.3** (not Arduino). PlatformIO manages the toolchain. After changing `sdkconfig.defaults`, delete `sdkconfig.esp32-s3-touch-lcd-7b` and `.pio/` to force regeneration.

**Flashing note**: VS Code PlatformIO serial monitor holds COM6. Either flash from VS Code (Upload button), or close the monitor / kill `python.exe` before CLI flash. The ESP32-S3 may need boot mode (hold BOOT + press RST) before flashing.

## Hardware

Waveshare ESP32-S3-Touch-LCD-7B: ESP32-S3 @ 240MHz, 8MB OPI PSRAM @ 120MHz, 16MB QIO Flash @ 120MHz, 7" 1024x600 RGB565 LCD (ST7262), GT911 capacitive touch (I2C @ 0x5D), CH422G IO expander (I2C @ 0x24).

## Architecture

### Dual-Core Task Layout

- **Core 1**: LVGL render loop (`lvgl_port_task`) — handles display updates and touch input
- **Core 0**: Network task (`network_task` in main.cpp) — WiFi, NTP sync, weather API polling every 30min, HA calendar fetch, transport polling every 1min

All LVGL API calls must be wrapped in `lvgl_port_lock(-1)` / `lvgl_port_unlock()`.

### Cross-Core Communication

Calendar day click on Core 1 triggers on-demand fetch on Core 0 via `xTaskNotifyGive()` / `ulTaskNotifyTake()`. Shared request vars (`s_req_year/month/day`) are set by UI, read by network task. The network loop wakes instantly on notification or times out after 10s for scheduled polling.

### Display Pipeline (anti-tear)

The LCD uses PSRAM framebuffers with bounce buffers (PSRAM → internal SRAM → LCD DMA). The anti-tear mechanism:

1. `lvgl_port.h` defines mode 3: double-buffer + direct mode (`LVGL_PORT_AVOID_TEAR_MODE=3`)
2. LVGL renders dirty areas into one of 2 PSRAM framebuffers obtained via `esp_lcd_rgb_panel_get_frame_buffer()`
3. `flush_callback()` calls `esp_lcd_panel_draw_bitmap()` then blocks on `ulTaskNotifyTake()`
4. `on_bounce_frame_finish` ISR (IRAM-safe) fires when bounce DMA completes → unblocks LVGL task

**Critical**: With bounce buffers, register **only** `on_bounce_frame_finish` (not `on_vsync`). Without bounce buffers, register **only** `on_vsync`. This matches Waveshare's reference implementation. Registering both causes flicker.

### PSRAM XIP

`sdkconfig.defaults` enables `SPIRAM_FETCH_INSTRUCTIONS` + `SPIRAM_RODATA` + `SPIRAM_TRY_ALLOCATE_WIFI_LWIP`. This moves code execution, read-only data, and WiFi/LWIP buffers to PSRAM, freeing internal SRAM for DMA. Without these, WiFi DMA competes with LCD DMA for SRAM, causing visible "jumping columns" artifacts.

### WiFi Manager

Event-driven using `esp_wifi` + `esp_event` + `EventGroupHandle_t`. Auto-reconnects on disconnect. Public API: `wifi_init(ssid, pass)`, `wifi_is_connected()`.

### Weather

Uses `esp_http_client` + `cJSON` (both built into ESP-IDF) to fetch from Open-Meteo API. SSL via `esp_crt_bundle_attach`. Weather icons drawn at runtime on a 48x48 LVGL canvas (no external assets). Public API: `weather_fetch_and_update()`, `weather_get_data()`, `weather_get_last_error()`.

### HA Calendar

Fetches events from Home Assistant calendar API. Supports on-demand date fetch (calendar day click) and auto-refresh every 5min. Filters out `workday_sensor_calendar`. Events are color-coded by source calendar (6-color palette). Public API: `ha_calendar_fetch_and_update()`, `ha_calendar_fetch_for_date(y,m,d)`, `ha_calendar_get_data()`.

### Transport

Fetches real-time bus arrivals from `transport-api.dzarlax.dev` (city-dashboard backend) for two stops at Đeram: stop 89 (outbound/oblast) and stop 90 (inbound/centar). Returns up to 5 vehicles per stop, sorted by arrival time. Uses HTTPS with `esp_crt_bundle_attach`. Vehicles are displayed with LVGL recolor — route number in highlight blue, arrival time color-coded green→yellow→orange→red based on proximity. Public API: `transport_fetch_and_update()`, `transport_get_data()`, `transport_get_last_error()`.

### Custom Fonts

Cyrillic support for HA calendar events uses a custom font generated with `lv_font_conv`. The font (`src/fonts/font_montserrat_16_cyr.c`) covers only Cyrillic range (0x400-0x4FF) with fallback to built-in `lv_font_montserrat_16` for Latin.

**Critical**: Generate fonts with `--no-compress --no-prefilter` flags — `LV_USE_FONT_COMPRESSED` is disabled in lv_conf.h, so compressed fonts render as invisible.

## UI Layout (1024x600)

```
┌─────────────────────────────────────────────────────┐
│ Top bar: datetime                    city + temp    │ 60px
├──────────────┬──────────────────────────────────────┤
│[icon] Weather│ Schedule title           [Today btn] │
│  H:% W: Tmrw│ event 1 (colored by calendar)        │
│──────────────│ event 2 ...                          │
│              │ (up to 8 events, 35px each)          │
│  Calendar    ├──────────────────────────────────────┤
│  410x510     │ Djeram                                │
│  (390x430)   │ → Oblast  5:3min  7L:8min  14:12min  │
│              │ ← Centar 6:10min  7L:15min           │
├──────────────┴──────────────────────────────────────┤
│ Bottom bar: WiFi, heap, W/Cal/Tr status (font 12)  │ 20px
└─────────────────────────────────────────────────────┘
```

Weather is in the left panel above the calendar. Transport is in the right panel below events. Transport uses `lv_label_set_recolor()` for color-coded arrival times (green=close, red=far).

## Key Configuration

- `src/config.h` — WiFi credentials, HA token/URL, weather location, NTP server, POSIX timezone
- `src/lv_conf.h` — LVGL features, fonts, memory (64KB pool), `LV_FONT_CUSTOM_DECLARE`
- `src/lvgl_port.h` — Anti-tear mode, buffer config, task priority/core
- `src/rgb_lcd_port.h` — LCD timings, bounce buffer size, GPIO pins
- `sdkconfig.defaults` — PSRAM speed, XIP, cache line size, ISR safety

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | App entry, network task, cross-core calendar/transport request |
| `src/ui_dashboard.cpp/.h` | All UI creation and update logic |
| `src/ha_calendar.cpp/.h` | HA calendar API fetch, date filtering, event parsing |
| `src/transport.cpp/.h` | Transport API fetch (stops 89/90), JSON parsing, sort by arrival |
| `src/weather.cpp/.h` | Open-Meteo API fetch and parsing |
| `src/weather_icons.cpp/.h` | Canvas-drawn weather icons (sun, cloud, rain, snow, etc.) |
| `src/wifi_manager.cpp/.h` | WiFi init, event-driven connect/reconnect |
| `src/lvgl_port.cpp/.h` | LVGL FreeRTOS integration, flush callback, anti-tear |
| `src/rgb_lcd_port.cpp/.h` | RGB LCD panel init, bounce buffers, GPIO pins |
| `src/gt911.cpp/.h` | GT911 capacitive touch I2C driver |
| `src/touch.cpp/.h` | Touch abstraction layer for LVGL |
| `src/i2c.cpp/.h` | I2C bus abstraction |
| `src/io_extension.cpp/.h` | CH422G IO expander driver |
| `src/fonts/font_montserrat_16_cyr.c` | Generated Cyrillic font (Montserrat 16, bpp4) |
| `src/CMakeLists.txt` | ESP-IDF component registration |
