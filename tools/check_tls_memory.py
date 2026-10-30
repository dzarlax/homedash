#!/usr/bin/env python3
"""Verify the effective ESP-IDF TLS allocation policy after a firmware build."""
import argparse
from pathlib import Path
import re


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=root / ".pio/build/esp32-s3-touch-lcd-7b/config/sdkconfig.h")
    args = parser.parse_args()
    values = dict(re.findall(r"^#define (CONFIG_\w+) (\S+)$", args.config.read_text(), re.M))
    for name in ("CONFIG_MBEDTLS_DEFAULT_MEM_ALLOC", "CONFIG_SPIRAM_USE_MALLOC", "CONFIG_MBEDTLS_CERTIFICATE_BUNDLE"):
        if values.get(name) != "1":
            raise SystemExit("Required compiled TLS setting missing: " + name)
    if values.get("CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC") == "1":
        raise SystemExit("TLS is still restricted to internal RAM")
    if values.get("CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL") != "64":
        raise SystemExit("Review the internal/PSRAM allocation threshold")
    print("TLS uses ESP-IDF malloc: allocations above 64 bytes prefer PSRAM; certificate bundle enabled")


if __name__ == "__main__":
    main()
