# HomeDash

A smart home dashboard for the **Waveshare ESP32-S3-Touch-LCD-7B** (7" 1024×600 touchscreen). All data flows through a single [ESP32 Bridge](https://github.com/dzarlax/esp32-bridge) server — the firmware contains **zero secrets** and can be published publicly.

## Features

- **4-page tileview** — swipe between pages:
  - **День**: weekly strip, closest calendar event, weather, transport and a home-air summary
  - **Сводка**: health metrics (readiness, steps, sleep, HR, HRV, SpO2), Todoist tasks and news
  - **Дом**: Home Assistant light controls and air-quality sensors per room
  - **Настройки**: Bridge web-panel link/QR, device status, local recovery and manual OTA check
- **OTA updates** — firmware updates over WiFi via Bridge, with automatic rollback
- **Web-managed preferences** — the Bridge settings page controls the display name, city/timezone, clock format, visible dashboard sections, idle/night behavior, photo timing, and managed update policy
- **Three UI languages** — Russian, English and Serbian Latin, with custom Montserrat fonts
- **Photo frame** — Bridge-prepared photos, server-rendered weather/event cards and a local clock/date; each information block can be enabled independently in the web panel
- **Dual-core** — LVGL renders on Core 1, all network I/O on Core 0
- **Anti-tear display** — PSRAM double-buffer + bounce buffer pipeline

## Hardware

| Component | Details |
|-----------|---------|
| Board | Waveshare ESP32-S3-Touch-LCD-7B |
| CPU | ESP32-S3 @ 240 MHz |
| RAM | 8 MB OPI PSRAM @ 120 MHz |
| Flash | 16 MB QIO @ 120 MHz |
| Display | 7" 1024×600 RGB565, ST7262 controller |
| Touch | GT911 capacitive (I2C @ 0x5D) |

## Architecture

```
ESP32 Bridge (Go server)
  ├─ /api/dashboard    → health, tasks, news, sensors, lights, weather, transport
  ├─ /api/calendar     → Calendar Platform events, with HA compatibility fallback
  ├─ /api/photo-frame/v1/manifest → prepared photo list
  ├─ /api/photo-frame/v1/frames/{id} → full-screen RGB565 photo
  ├─ /api/photo-frame/v1/context → clock preferences and card metadata
  ├─ /api/photo-frame/v1/overlays/{id} → server-rendered RGB565 cards
  ├─ /api/ha/action    → light toggle (returns fresh state)
  ├─ /api/ota/check    → firmware version comparison
  └─ /api/ota/firmware → streams firmware binary for OTA
         ↕
    ESP32 Display
      NVS: wifi_ssid, wifi_pass, bridge_url, bridge_key
      Polls /api/dashboard every 30s
      Syncs web preferences every 10s on /api/device/v1/sync
      Checks OTA every 30 min (or manual button in Settings)
      NVS namespace `webidentity`: stable 32-hex device ID shared by all native requests
      NVS namespace `webprefs`: optional versioned settings, revision, command replay marker, network trial journal
```

All secrets (WiFi credentials, Bridge URL, API key) are stored in the **NVS partition**, not in the firmware binary.

One Bridge can manage multiple displays with shared service connections and independent
screen preferences, source selections and photo albums. Each display creates a stable
`device_id` in NVS namespace `webidentity` and sends `X-Device-ID` on dashboard,
calendar, Home Assistant, photo, OTA and settings requests. Settings reset and network
changes preserve that identity. The display registers on its first authenticated
settings sync; select it in the Bridge web panel. Headerless legacy clients retain
the existing default Bridge feed.

The display supports Russian, English and Serbian Latin (`ru`, `en`, `sr-Latn`).
Choose the device language in the Bridge Screen settings; the web panel language
is independent. Changes apply to existing UI objects and persist across reboot.
User content such as calendar titles, names and news stays as supplied by services.
Legacy preferences and network journals keep their binary layout: locale uses a
separate versioned NVS record scoped to Bridge ID and revision. Older Bridge
responses without a locale select Russian. No firmware update is implied by
changing the browser language.

Use the Bridge web settings page for managed display preferences, including language. The fourth display tab, Settings, provides a public web link/QR, device status and manual OTA check. Local recovery starts only from its explicit button (or automatically when initial network configuration is missing). The temporary protected setup network lasts ten minutes and can be stopped explicitly. Its password stays masked until tapped, is revealed for 15 seconds, and hides on navigation, screen off, photo mode or a language change. Leaving Settings keeps the network active for phone setup. Network changes are tried before they are committed, and an interrupted trial restores the previous NVS configuration on boot.

### Dual-Core Layout

| Core | Task | Responsibility |
|------|------|----------------|
| Core 1 | `lvgl_port_task` | LVGL render loop @ 60 FPS, touch input |
| Core 0 | `network_task` | WiFi, NTP, Bridge polling, OTA, on-demand calendar/light requests |

### Partition Table

```
nvs       24KB   — NVS config (WiFi, Bridge credentials)
otadata    8KB   — OTA slot tracker
phy_init   4KB   — radio calibration
ota_0      7MB   — firmware slot A
ota_1      7MB   — firmware slot B
```

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- ESP-IDF 5.3+ (managed automatically by PlatformIO)
- Python 3 (for NVS binary generation)
- A running [ESP32 Bridge](https://github.com/dzarlax/esp32-bridge) instance

### First-Time Setup

1. **Build and flash firmware** (USB required once):
   ```bash
   pio run -t upload
   ```

2. **Generate NVS binary** with your credentials:
   ```bash
   python3 tools/gen_nvs.py
   ```
   You'll be prompted for:
   - WiFi SSID and password
   - Bridge URL (e.g. `https://esp32-bridge.example.com`)
   - Bridge API key

3. **Flash NVS** to the device:
   ```bash
   esptool.py --port /dev/cu.usbmodemXXX write_flash 0x9000 nvs.bin
   ```

4. **Done.** All future updates are delivered via OTA.

### Releasing a New Version

1. Bump `FW_VERSION` in `src/config.h`
2. Commit, push, and create a tag:
   ```bash
   git tag v1.0.1
   git push --tags
   ```
3. GitHub Actions builds firmware and creates a Release with `firmware.bin`
4. Ensure Bridge `.env` points at the firmware repository:
   ```
   OTA_GITHUB_REPO=dzarlax/homedash
   ```
5. The display picks up the new GitHub Release within 30 minutes (or tap "Проверить обновление" in Settings).

### OTA Rollback

If the new firmware fails to connect to Bridge after boot, the bootloader automatically reverts to the previous working version (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`).

## Desktop UI Simulator

Network/bootstrap regression checks run without a device:

```bash
sh sim/settings_test/run.sh
sh sim/sync_client_test/run.sh
sh sim/ota_test/run.sh
sh sim/photo_client_test/run.sh
make -C sim test-network test-calendar
pio run
python3 tools/check_network_stack.py
```

The sync client checks include address/undefined sanitizers, bounded responses,
identity/revision rejection and response-allocation failures. The stack check
budgets compiled application frames before HTTP/TLS; it does not measure the
complete runtime TLS stack or replace verification on the display.

You can preview the real LVGL UI without flashing the ESP32-S3:

```powershell
powershell -ExecutionPolicy Bypass -File sim\build_windows.ps1 -Run
```

On macOS or Linux with SDL2 installed:

```bash
make -C sim clean all
./sim/homedash_sim
```

The simulator opens a 1024x600 desktop window, compiles the real `src/ui_dashboard.cpp`, and uses local fixture data. It supports swipe and the persistent `День` / `Самочувствие` / `Дом` navigation. It is intended for layout and interaction checks only; it does not validate LCD timing, PSRAM/DMA behavior, WiFi, OTA, or the GT911 touch controller. Fixture actions are local only: neither light controls nor OTA can affect a real device.

The production firmware keeps Bridge credentials in NVS. Do not put a Bridge URL or API key in simulator source code, command arguments, or logs.

## Key Files

| File | Purpose |
|------|---------|
| `src/main.cpp` | App entry, network task, cross-core coordination |
| `src/ui_dashboard.cpp/.h` | All UI creation and update logic (3 pages) |
| `src/bridge.cpp/.h` | Bridge API client — dashboard, calendar, light toggle |
| `src/ota.cpp/.h` | OTA update module (check + download via Bridge) |
| `src/nvs_config.cpp/.h` | NVS credential reader |
| `src/weather_icons.cpp/.h` | Canvas-drawn weather icons |
| `src/wifi_manager.cpp/.h` | Event-driven WiFi connect/reconnect |
| `src/web_identity.cpp/.h` | Persistent device identity and native request header |
| `src/web_sync.cpp/.h` | Bounded authenticated settings heartbeat and capability report |
| `src/web_settings.cpp/.h` | Settings schema validation and defaults |
| `src/web_settings_store.cpp/.h` | Versioned preferences, command replay markers, and network rollback journal |
| `src/recovery_ap.cpp/.h` | Short-lived protected local setup page |
| `src/config.h` | Compile-time constants (version, NTP, timezone) |
| `tools/gen_nvs.py` | NVS binary generator |
| `partitions.csv` | Dual OTA partition layout |

## Custom Fonts

Russian and Serbian Latin use generated Montserrat glyphs with the existing
built-in ASCII fallback. The checked-in source font and SIL OFL license are in
[src/fonts](src/fonts/README.md). Regenerate both 16/24 fonts with the pinned
`lv_font_conv` version and `tools/generate_ui_fonts.sh`; the generator includes
Cyrillic, Latin Extended-A and the middle dot. Do not edit glyph bitmap C by hand.

Run `make -C sim test-localization` for real LVGL locale-cycle, content-preservation,
reader-scroll, glyph and fixed-label layout checks. For snapshots of all pages:

```sh
bash sim/localization_test/run.sh /tmp/homedash-localization-screenshots
```

The simulator checks layout and UI behavior; hardware networking, PSRAM, touch
and OTA installation require separate device acceptance.

## Photo frame

Available in firmware **v1.0.27**, with a compatible Bridge. Bridge prepares
1024×600 RGB565 photos and small weather/event cards; the ESP32 displays them and
updates its 80px clock and date locally, in the configured language, timezone and
12/24-hour format. A minute change does not download another photograph.

Configure the source, album, idle delay, slide interval and the **Clock and date**,
**Weather**, and **Next event** switches in the Bridge web panel. Missing visibility
settings default to enabled. Manual launch is available from that panel; Home no
longer contains a photo-frame button or loading/status text. A touch exits to the
previous dashboard page and cancels a pending manual start.

Photos use centered crop when it removes at most 10% by default. Beyond that
threshold, one complete photo is centered over a dimmed, blurred copy. Portraits
remain whole. The clock does not add a full-width shaded strip to the photo.

While the frame is visible, the display polls context once per minute and replaces
only changed cards. Hashes, dimensions, placement and expiration are validated;
expired cards disappear locally even when the network is unavailable. Context
updates preserve the current photo and slide timer. Cards use bounded PSRAM
buffers and share the existing sequential photo worker. Background dashboard
activity does not dismiss the frame.

Bridge chooses an ongoing timed event first, then the next event within 24 hours
from the selected calendars. All-day events are excluded. If the calendar response
is partial, only explicitly ready, non-stale sources are used, with a "From available calendars" caption. The date browsed on the Day tab does not affect this selection.
Weather expires after 30 minutes without fresh source data; event cards expire
after five minutes or at the next start/end transition, whichever comes first.

Adaptive photo-matched cards and local light/dark clock text are available in
firmware **v1.0.28**. With a
compatible Bridge, the display requests v2 context for the displayed frame and
rejects late responses for another frame. Up to three small RGB565 patches share
a 512 KiB aggregate pixel budget including replacement staging; no third full
photo buffer is allocated. Older Bridges continue to use v1 context. Hardware
memory and long-running Wi-Fi acceptance require a separately authorized release.

Firmware **v1.0.29** negotiates persistent v3 information
with a compatible Bridge. Weather and event content use straight-alpha BGRA
images over native translucent panels; the clock is drawn locally. The panels
remain visible across photo changes and are independent of the photo source.
V1 and v2 remain available for older Bridges. Hardware memory and long-running
Wi-Fi acceptance remain separate from server and simulator checks.

### Simulator and checks

The desktop simulator can exercise the photo frame using generated color bars and
no network credentials: set `HOMEDASH_SIM_PHOTO_FRAME=1` before launching the
simulator. Fixture time runs 30 times faster (the default idle period takes ten
seconds); touch or click dismisses the frame. This does not test ESP32 PSRAM,
TLS, display timing, or OTA.

The default idle period is five minutes. Managed preferences can override it
from 30 to 3600 seconds, including saved preferences restored at boot. Without
managed preferences, manifest timing applies after the first successful request.

Run `make -C sim test-photo-frame test-photo-client` for fake-clock UI and
HTTP-client protocol checks. The client test needs a C++ compiler and OpenSSL
development headers; it uses temporary files and a mocked transport.

Run `make -C sim test-settings` for host checks of settings validation, NVS
rollback/commit behavior, command replay, and post-reboot command results.

Photo-frame loading and manifest refresh retain the current image until the next
frame has downloaded successfully. OTA checks also leave the photo frame active;
only an available update pauses the worker and releases its buffers before
flashing. If the worker cannot acknowledge the pause, flashing is postponed.

## Settings application and Home action feedback

The web panel distinguishes saving on Bridge from the display acknowledging the current revision and locale. It keeps the last known status if polling fails and offers a read-only status check. A lost save response remains uncertain until reconciliation; write-only credentials are never inferred from readable settings. Draft edits survive polling and an in-flight save.

Home light commands use explicit on/off intents, and climate temperature taps debounce for 500 ms. Controls for the affected entity stay disabled while its command is active. Only an available, freshly read matching Home Assistant state confirms an accepted command. A lost response remains unknown; subsequent matching reads report updated state without claiming causal success. No action is automatically resent. The 20-second UI waiting budget does not imply cancellation of an already transmitted command; ESP HTTP I/O timeouts are not a strict TLS wall-clock bound.

Run `make -C sim test-action-feedback test-action-bridge test-home test-settings-tab test-photo-frame test-localization` for isolated tracker, scheduling, HTTP, and LVGL regressions. Host tests require the existing local cJSON source/header in `sim/esp_stubs/`; these are ignored simulator dependencies. Windows source lists include settings and QR dependencies, but Windows compilation must be verified on Windows.

## Related

- [ESP32 Bridge](https://github.com/dzarlax/esp32-bridge) — Go backend that aggregates all data sources and proxies OTA updates

## License

MIT
