#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/photo-client-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
# The simulator vendors real cJSON. OpenSSL supplies real SHA-256 through a
# small mbedTLS adapter. Override PKG_CONFIG or compiler via environment.
: "${CC:=cc}" "${CXX:=c++}" "${PKG_CONFIG:=pkg-config}"
if command -v "$PKG_CONFIG" >/dev/null 2>&1 && "$PKG_CONFIG" --exists openssl; then
  ssl_cflags=$($PKG_CONFIG --cflags openssl)
  ssl_libs=$($PKG_CONFIG --libs openssl)
elif [ -f /opt/homebrew/opt/openssl/include/openssl/sha.h ]; then
  ssl_cflags=-I/opt/homebrew/opt/openssl/include
  ssl_libs="-L/opt/homebrew/opt/openssl/lib -lcrypto"
else
  ssl_cflags=${OPENSSL_CFLAGS:-}
  ssl_libs=${OPENSSL_LIBS:--lcrypto}
fi
$CC -std=c99 -I"$root/sim/esp_stubs" -c "$root/sim/esp_stubs/cJSON.c" -o "$tmp/cJSON.o"
$CXX -std=c++17 -Wno-deprecated-declarations $ssl_cflags -I"$here/include" -I"$root/sim/esp_stubs" -I"$root/src" "$here/test.cpp" "$root/src/photo_frame_client.cpp" "$tmp/cJSON.o" $ssl_libs -o "$tmp/test"
"$tmp/test"
