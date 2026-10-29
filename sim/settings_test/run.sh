#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/homedash-settings-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -I"$root/sim/esp_stubs" -c "$root/sim/esp_stubs/cJSON.c" -o "$tmp/cJSON.o"
${CXX:-c++} -std=c++17 -Wall -Wextra -I"$here" -I"$root/sim/esp_stubs" -I"$root/src" \
  "$here/test.cpp" "$root/src/web_settings.cpp" "$root/src/web_settings_store.cpp" \
  "$root/src/web_identity.cpp" \
  "$root/src/nvs_config.cpp" "$root/src/recovery_security.cpp" "$tmp/cJSON.o" -o "$tmp/settings-test"
"$tmp/settings-test"
