#!/bin/sh
set -eu
: "${MBEDTLS_DIR:?Set MBEDTLS_DIR to ESP-IDF components/mbedtls/mbedtls}"
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/voice-cross.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cmake -S "$MBEDTLS_DIR" -B "$tmp/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF >/dev/null
cmake --build "$tmp/mbedtls" --target mbedcrypto -j 4 >/dev/null
c++ -std=c++11 -Wall -Wextra -DESP_PLATFORM -DVOICE_PAIR_STORE_TEST -I"$root/src" -I"$root/sim/voice_pair_store_test/include" -I"$root/sim/voice_pair_test/include" -I"$MBEDTLS_DIR/include" "$root/src/voice_pair_protocol.cpp" "$root/src/voice_pair_store.cpp" "$root/src/voice_pair_runtime.cpp" "$root/sim/voice_pair_test/random.cpp" "$root/sim/voice_pair_cross/device.cpp" "$tmp/mbedtls/library/libmbedcrypto.a" "$tmp/mbedtls/3rdparty/everest/libeverest.a" "$tmp/mbedtls/3rdparty/p256-m/libp256m.a" $(curl-config --cflags --libs) -o "$tmp/device"
cd "${VOICE_BRIDGE_DIR:-$root/../bridge}"
GOCACHE="${GOCACHE:-$tmp/gocache}" VOICE_FIRMWARE_FIXTURE="$tmp/device" go test ./internal/voicepairsetup -run '^TestFirmwareRuntimeTLSCrossLanguage$' -count=1 -timeout=30s -v
