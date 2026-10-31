#!/usr/bin/env python3
"""Reject compiled RGB settings that allow PSRAM refill with the cache disabled."""
import argparse
from pathlib import Path
import re


def check_config(header):
    values = dict(re.findall(r"^#define\s+(CONFIG_\w+)\s+(\S+)\s*$", header, re.M))
    for name in ("CONFIG_IDF_TARGET_ESP32S3", "CONFIG_SPIRAM",
                 "CONFIG_SPIRAM_FETCH_INSTRUCTIONS", "CONFIG_SPIRAM_RODATA"):
        if values.get(name) != "1":
            raise ValueError("Required compiled RGB/PSRAM setting missing: " + name)
    for name in ("CONFIG_LCD_RGB_ISR_IRAM_SAFE", "CONFIG_GDMA_ISR_IRAM_SAFE"):
        if name in values and values[name] != "0":
            raise ValueError("PSRAM bounce-buffer refill must wait for cache availability: " + name)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path,
                        default=root / ".pio/build/esp32-s3-touch-lcd-7b/config/sdkconfig.h")
    args = parser.parse_args()
    try:
        check_config(args.config.read_text())
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
    print("RGB cache guard passed: LCD/GDMA IRQs deferred while cache is disabled; PSRAM XIP retained")


if __name__ == "__main__":
    main()
