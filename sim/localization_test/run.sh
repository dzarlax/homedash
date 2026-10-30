#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "$0")/../.." && pwd)"
out_dir="${1:-/tmp/homedash-localization-screenshots}"
mkdir -p "$out_dir"
make -C "$repo_root/sim" homedash_sim

for locale in ru en sr-Latn; do
  HOMEDASH_SIM_LOCALE_TEST=1 \
  HOMEDASH_SIM_SCREENSHOT_LOCALE="$locale" \
  HOMEDASH_SIM_SCREENSHOT_PATH="$out_dir/${locale}-page1.bmp" \
  HOMEDASH_SIM_SCREENSHOT_PAGE=0 \
  SDL_VIDEODRIVER=dummy "$repo_root/sim/homedash_sim"
done

for locale in en sr-Latn; do
  for page in 1 2; do
    HOMEDASH_SIM_LOCALE_TEST=1 \
    HOMEDASH_SIM_SCREENSHOT_LOCALE="$locale" \
    HOMEDASH_SIM_SCREENSHOT_PATH="$out_dir/${locale}-page$((page + 1)).bmp" \
    HOMEDASH_SIM_SCREENSHOT_PAGE="$page" \
    SDL_VIDEODRIVER=dummy "$repo_root/sim/homedash_sim"
  done
done

printf 'Screenshots saved under %s\n' "$out_dir"
