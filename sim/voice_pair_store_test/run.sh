#!/bin/sh
set -eu
: "${MBEDTLS_DIR:?Set MBEDTLS_DIR to ESP-IDF components/mbedtls/mbedtls}"
CMAKE=${CMAKE:-cmake}; CXX=${CXX:-c++}
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/voice-pair-store-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"$CMAKE" -S "$MBEDTLS_DIR" -B "$tmp/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF >/dev/null
"$CMAKE" --build "$tmp/mbedtls" --target mbedcrypto -j 4 >/dev/null
"$CXX" -std=c++11 -Wall -Wextra -Werror -DVOICE_PAIR_STORE_TEST -DESP_PLATFORM -I"$root/src" -I"$root/sim/voice_pair_store_test/include" -I"$root/sim/voice_pair_test/include" -I"$MBEDTLS_DIR/include" "$root/src/voice_pair_protocol.cpp" "$root/src/voice_pair_store.cpp" "$root/src/voice_pair_runtime.cpp" "$root/sim/voice_pair_test/random.cpp" "$root/sim/voice_pair_store_test/test.cpp" "$tmp/mbedtls/library/libmbedcrypto.a" "$tmp/mbedtls/3rdparty/everest/libeverest.a" "$tmp/mbedtls/3rdparty/p256-m/libp256m.a" -o "$tmp/test"
"$tmp/test"
printf '%s\n' 'NVS journal tests passed'
