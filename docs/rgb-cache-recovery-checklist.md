# RGB cache recovery acceptance

Candidate: v1.0.38. Local build checks do not establish hardware acceptance.

## Before installation

- Build both PlatformIO environments from clean sources without generated sdkconfig
  files. Run the RGB, network stack, and TLS guards against each generated build.
- Confirm the RGB guard rejects the v1.0.36 generated header.
- Preserve the ESP-IDF pin, framebuffer/bounce buffers, clocks, and the single
  frame-finish callback. Do not enable both VSYNC and bounce-frame callbacks.
- Obtain explicit release/install permission and fresh device readiness before OTA.
  Capture UART with private logs; do not publish Wi-Fi credentials or API keys.

## Preferred path: OTA through Bridge

- Wait for a stable boot, applied settings, and a healthy Bridge connection before
  sending exactly one authorized install command. Record the command ID and result.
- Confirm v1.0.38 in UART and Bridge, not merely a published GitHub Release.
- Confirm successful app validation. A crash before the old app selects the new
  slot can still occur in v1.0.36; this does not test the candidate's runtime.
- If interrupted, preserve the log and inspect before another attempt. Do not
  repeat installs or switch to USB automatically.

## USB fallback (only if separately authorized)

- Stop the UART logger, confirm bootloader mode/readiness, and read the current
  partition/OTA data before choosing a write address.
- Save private, hashed backups of the active application slot and OTA data.
- Write and verify only the intended application slot. Preserve NVS, partition
  table, bootloader, and the other app slot; never erase the whole flash.
- Restoring backups also requires explicit permission. USB recovery alone does
  not prove that OTA download and activation work.

## Physical acceptance (pending)

- Verify calendar and all three Summary blocks load.
- Change pages and enter/exit the photo frame repeatedly; check for flicker,
  persistent drift, tearing, stalled display, and cache panic.
- Confirm saved day/night brightness and settings revision remain unchanged.
- Perform three agreed restarts, confirm boot validation and Bridge reconnection
  each time, and check the private UART log for panic/reset causes.
- If IRQ deferral causes persistent display corruption, stop and review the result
  before changing buffering, SDK version, or other display timing settings.
