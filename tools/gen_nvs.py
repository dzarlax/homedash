#!/usr/bin/env python3
"""Generate NVS binary for HomeDash ESP32 display.

Usage:
    python3 tools/gen_nvs.py

Prompts for WiFi and Bridge credentials, generates nvs.bin.
Flash with: esptool.py --port /dev/cu.usbmodemXXX write_flash 0x9000 nvs.bin
"""

import csv
import os
import subprocess
import sys
import tempfile

NVS_PARTITION_SIZE = 0x6000  # 24KB, must match partitions.csv


def find_nvs_gen():
    """Find nvs_partition_gen.py in PlatformIO ESP-IDF installation."""
    pio_root = os.path.expanduser("~/.platformio")
    for root, dirs, files in os.walk(pio_root):
        if "nvs_partition_gen.py" in files:
            return os.path.join(root, "nvs_partition_gen.py")
    return None


def main():
    print("=== HomeDash NVS Config Generator ===\n")

    wifi_ssid = input("WiFi SSID: ").strip()
    wifi_pass = input("WiFi Password: ").strip()
    bridge_url = input("Bridge URL (e.g. https://esp32-bridge.dzarlax.dev): ").strip()
    bridge_key = input("Bridge API Key: ").strip()

    if not all([wifi_ssid, wifi_pass, bridge_url, bridge_key]):
        print("Error: all fields are required.")
        sys.exit(1)

    # Write CSV for nvs_partition_gen
    csv_path = os.path.join(os.path.dirname(__file__), "..", "nvs_config.csv")
    csv_path = os.path.abspath(csv_path)

    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["key", "type", "encoding", "value"])
        writer.writerow(["config", "namespace", "", ""])
        writer.writerow(["wifi_ssid", "data", "string", wifi_ssid])
        writer.writerow(["wifi_pass", "data", "string", wifi_pass])
        writer.writerow(["bridge_url", "data", "string", bridge_url])
        writer.writerow(["bridge_key", "data", "string", bridge_key])

    print(f"\nCSV written to: {csv_path}")

    # Find and run nvs_partition_gen.py
    nvs_gen = find_nvs_gen()
    if not nvs_gen:
        print("\nWarning: nvs_partition_gen.py not found in ~/.platformio")
        print("Install PlatformIO or run manually:")
        print(f"  python3 <path>/nvs_partition_gen.py generate {csv_path} nvs.bin {NVS_PARTITION_SIZE:#x}")
        return

    out_path = os.path.join(os.path.dirname(__file__), "..", "nvs.bin")
    out_path = os.path.abspath(out_path)

    cmd = [sys.executable, nvs_gen, "generate", csv_path, out_path, str(NVS_PARTITION_SIZE)]
    print(f"\nRunning: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode == 0:
        print(f"\n✓ NVS binary generated: {out_path}")
        print(f"\nFlash with:")
        print(f"  esptool.py --port /dev/cu.usbmodemXXX write_flash 0x9000 {out_path}")
    else:
        print(f"\nError generating NVS binary:")
        print(result.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
