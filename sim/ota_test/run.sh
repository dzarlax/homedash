#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/homedash-ota-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
${CC:-cc} -std=c99 -I"$root/sim/esp_stubs" -c "$root/sim/esp_stubs/cJSON.c" -o "$tmp/cJSON.o"
${CXX:-c++} -std=c++17 -I"$here/include" -I"$root/sim/photo_client_test/include" -I"$root/sim/esp_stubs" -I"$root/src" "$here/test.cpp" "$root/src/ota.cpp" "$tmp/cJSON.o" -o "$tmp/test"
"$tmp/test"
