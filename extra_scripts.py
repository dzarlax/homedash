"""
Pre-build script: disable ARM-specific assembly files (Helium, NEON) that
can't compile on Xtensa (ESP32-S3). LVGL 9 includes these; PlatformIO
tries to compile them without checking architecture.
"""
Import("env")
import os, glob

LVGL_SRC = os.path.join(
    env.get("PROJECT_LIBDEPS_DIR", ".pio/libdeps"),
    "esp32-s3-touch-lcd-7b",
    "lvgl", "src"
)

for asm_file in glob.glob(os.path.join(LVGL_SRC, "**", "*.S"), recursive=True):
    disabled = asm_file + ".disabled"
    if not os.path.exists(disabled):
        os.rename(asm_file, disabled)
        print(f"[homedash] Disabled ARM ASM: {os.path.relpath(asm_file, LVGL_SRC)}")
