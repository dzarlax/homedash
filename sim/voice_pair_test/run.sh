#!/bin/sh
# Host-only tests; MBEDTLS_DIR must point to the same mbedTLS source used by ESP-IDF.
set -eu
: "${MBEDTLS_DIR:?Set MBEDTLS_DIR to ESP-IDF components/mbedtls/mbedtls}"
CMAKE=${CMAKE:-cmake}
CXX=${CXX:-c++}
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/voice-pair-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"$CMAKE" -S "$MBEDTLS_DIR" -B "$tmp/mbedtls" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF >/dev/null
"$CMAKE" --build "$tmp/mbedtls" --target mbedcrypto -j 4 >/dev/null
build() {
  "$CXX" -std=c++11 -Wall -Wextra -Werror -I"$root/src" -I"$root/sim/voice_pair_test/include" -I"$MBEDTLS_DIR/include" "$root/src/voice_pair_protocol.cpp" "$root/sim/voice_pair_test/random.cpp" "$1" "$tmp/mbedtls/library/libmbedcrypto.a" "$tmp/mbedtls/3rdparty/everest/libeverest.a" "$tmp/mbedtls/3rdparty/p256-m/libp256m.a" -o "$2"
}
"$CXX" -std=c++11 -Wall -Wextra -Werror -pthread -I"$root/src" "$root/sim/voice_pair_test/entropy_test.cpp" -o "$tmp/entropy"
"$tmp/entropy"
build "$root/sim/voice_pair_protocol_test.cpp" "$tmp/unit"
"$tmp/unit"
build "$root/sim/voice_pair_test/vector.cpp" "$tmp/vector"
GOCACHE="$tmp/gocache" go run "$root/sim/voice_pair_test/vector.go" "$tmp/vector"
