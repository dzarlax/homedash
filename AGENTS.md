# AGENTS.md

Guidance for Codex and other coding agents working in this repository.

## Project

HomeDash firmware for the Waveshare ESP32-S3-Touch-LCD-7B, built with PlatformIO and ESP-IDF. The display talks only to ESP32 Bridge; WiFi credentials, Bridge URL, and API key live in NVS, not in the firmware binary.

The active UI is a 3-page LVGL tileview:

1. Weather, Home Assistant calendar, and transport arrivals.
2. Health metrics, Todoist tasks, and news.
3. Home Assistant light controls, room air sensors, and manual OTA update.

AgentDeck was removed in May 2026. Do not reintroduce the fourth page, WebSocket client, mDNS dependency, or `agentdeck` source files unless the user explicitly asks for that feature again.

## Commands

Build locally:

```powershell
C:\Users\me\.platformio\penv\Scripts\pio.exe run
```

Generic PlatformIO equivalents:

```bash
pio run
pio run -t upload
pio device monitor
```

Do not flash firmware unless the user explicitly confirms the device is ready and in the correct mode. This includes `pio run -t upload`, `esptool.py write_flash`, and any equivalent upload command.

Run the Windows desktop UI simulator:

```powershell
powershell -ExecutionPolicy Bypass -File sim\build_windows.ps1 -Run
```

The simulator uses Visual Studio C++ tools, a Win32 window, LVGL 9, and local fixtures. It is for UI layout and interaction checks before flashing; it does not validate ESP32 display timing, PSRAM/DMA, WiFi, OTA, or the physical touch controller.

## Toolchain

`platformio.ini` intentionally pins:

```ini
platform = https://github.com/pioarduino/platform-espressif32.git#53.03.13
```

Keep this pin unless there is a deliberate ESP-IDF upgrade task. The floating `espressif32` platform pulled an incompatible ESP-IDF 6.x stack in GitHub Actions and broke release builds. The pinned pioarduino platform uses ESP-IDF 5.3.2, matching the known-good local build.

LVGL is pinned through `lib_deps`:

```ini
lvgl/lvgl@9.2.2
```

## OTA Release Flow

Before creating a release:

1. Bump `FW_VERSION` in `src/config.h`.
2. Run a local build.
3. Commit and push the change.
4. Create and push a version tag, for example `v1.0.3`.
5. Wait for the GitHub Actions `Release` workflow to complete.
6. Verify that the GitHub Release is not draft/prerelease and contains `firmware.bin`.
7. Verify that `repos/dzarlax/homedash/releases/latest` resolves to the new tag.

The Bridge OTA configuration should point at:

```env
OTA_GITHUB_REPO=dzarlax/homedash
```

The device checks OTA through Bridge, not by embedding a firmware URL directly in the display firmware. The display checks automatically on its interval, and the user can trigger a manual check from Page 3.

## Generated Files

Do not edit build output or generated dependency output directly:

- `.pio/`
- `dependencies.lock`
- `managed_components/`
- generated firmware binaries

`dependencies.lock` and `managed_components/` are ignored because ESP-IDF component manager may create them during local builds.

## Key Files

- `src/main.cpp` - app entry, network task, OTA polling, Bridge polling.
- `src/ui_dashboard.cpp` - all three UI pages and refresh logic.
- `src/bridge.cpp` - Bridge API client.
- `src/ota.cpp` - OTA check and firmware download through Bridge.
- `src/config.h` - firmware version and compile-time config.
- `src/lv_conf.h` - LVGL feature and font configuration.
- `platformio.ini` - board, toolchain, partitions, LVGL dependency.
- `partitions.csv` - dual OTA partition layout.
- `tools/gen_nvs.py` - NVS binary generator for first-time setup.
- `sim/main_sim.cpp` - Windows LVGL simulator entrypoint.
- `sim/stubs.cpp` - simulator fixtures and hardware/network stubs.
- `sim/build_windows.ps1` - simulator build/run script.

## Performance Notes

LVGL feature flags in `src/lv_conf.h` were trimmed after AgentDeck removal. Keep unused widgets, layouts, themes, and font sizes disabled unless a new UI feature needs them.

Do not disable `LV_USE_IMAGE` casually. LVGL canvas internals depend on image support in this configuration, and disabling it breaks the build even if the UI does not display external image assets.

All LVGL API calls from non-LVGL tasks must be wrapped with:

```cpp
lvgl_port_lock(-1);
// LVGL calls
lvgl_port_unlock();
```

## Hardware Notes

The display pipeline uses PSRAM double buffering and bounce buffers to avoid tearing. Preserve the current anti-tear behavior unless testing on the actual device:

- With bounce buffers, register only `on_bounce_frame_finish`.
- Without bounce buffers, register only `on_vsync`.

Registering both callbacks caused flicker in prior testing.

## Release History Notes

`v1.0.2` was pushed during the AgentDeck removal release attempt, but its GitHub Actions release build failed before creating a GitHub Release. It should not affect OTA because Bridge follows the latest published GitHub Release.

`v1.0.3` is the successful AgentDeck removal OTA release. It contains `firmware.bin` and pins the PlatformIO ESP-IDF platform for reproducible CI builds.
