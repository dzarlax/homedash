#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
font_conv="${LV_FONT_CONV:-lv_font_conv}"
expected_version="1.5.3"
actual_version="$("$font_conv" --version)"
if [[ "$actual_version" != "$expected_version" ]]; then
  printf 'Expected lv_font_conv %s, found %s\n' "$expected_version" "$actual_version" >&2
  exit 1
fi

cd "$repo_root"
font="src/fonts/Montserrat-Medium.ttf"
for size in 16 24; do
  "$font_conv" --font "$font" \
    --range 0xB3,0xB5,0xB7,0x100-0x17F,0x400-0x4FF,0x2014 \
    --size "$size" --bpp 4 --no-compress --no-prefilter \
    --format lvgl --lv-include lvgl.h \
    --lv-font-name "font_montserrat_${size}_cyr" \
    --lv-fallback "lv_font_montserrat_${size}" \
    -o "src/fonts/font_montserrat_${size}_cyr.c"
done
