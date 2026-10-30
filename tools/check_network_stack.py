#!/usr/bin/env python3
"""Check application stack frames on the compiled ESP32 sync/TLS call path.

This is a regression budget, not a measurement of the full TLS runtime stack.
Run after `pio run`; the pinned Xtensa compiler emits the inspected ENTRY frames.
"""
import argparse
from pathlib import Path
import re
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=root / ".pio/build/esp32-s3-touch-lcd-7b")
    parser.add_argument("--objdump", type=Path)
    args = parser.parse_args()
    objdump = args.objdump
    if objdump is None:
        directory = Path.home() / ".platformio/packages/toolchain-xtensa-esp-elf/bin"
        objdump = next((p for p in directory.glob("xtensa-esp32s3-elf-objdump*") if p.is_file()), None)
    if objdump is None:
        parser.error("Xtensa objdump not found; supply --objdump")
    frames = {}
    for module in ("main", "web_sync"):
        result = subprocess.check_output(
            [str(objdump), "-d", "-C", str(args.build / "src" / (module + ".cpp.o"))], text=True
        )
        for name, amount in re.findall(r"^[0-9a-f]+ <([^\n]+)>:\n[^\n]*\bentry\s+a1, 0x([0-9a-f]+)", result, re.M):
            for prefix, key in (("network_task(", "network"), ("web_sync_poll(", "sync"),
                                ("(anonymous namespace)::post_payload(", "post")):
                if name.startswith(prefix):
                    frames[key] = int(amount, 16)
    if set(frames) != {"network", "sync", "post"}:
        raise SystemExit("Missing compiled frames; review compiler output: " + repr(frames))
    print("Application frames before HTTP/TLS:", frames, "sum =", sum(frames.values()), "bytes")
    if frames["network"] > 4096 or frames["sync"] > 2048 or sum(frames.values()) > 8192:
        raise SystemExit("Network stack regression: reserve TLS headroom in the 32 KiB task")


if __name__ == "__main__":
    main()
