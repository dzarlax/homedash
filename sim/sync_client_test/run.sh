#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/homedash-sync-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -Wno-deprecated-declarations -fsanitize=address,undefined -I"$root/sim/esp_stubs" -c "$root/sim/esp_stubs/cJSON.c" -o "$tmp/cJSON.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -fsanitize=address,undefined -pthread \
 -I"$here/include" -I"$root/sim/ota_test/include" \
 -I"$root/sim/esp_stubs" -I"$root/src" "$here/test.cpp" "$root/src/web_sync.cpp" \
 "$root/src/web_settings.cpp" "$tmp/cJSON.o" -o "$tmp/test"
"$tmp/test"
