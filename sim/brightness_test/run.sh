#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
c++ -std=c++17 -Isrc src/brightness_controller.cpp sim/brightness_test/test.cpp -o "$tmp/controller"
"$tmp/controller"
c++ -std=c++17 -Isim/brightness_test/include -Isrc src/io_extension.cpp sim/brightness_test/driver_test.cpp -o "$tmp/driver"
"$tmp/driver"

c++ -std=c++17 -pthread -Isim/brightness_test/include -Isrc src/brightness_calibration.cpp src/brightness_controller.cpp src/brightness_runtime.cpp sim/brightness_test/runtime_freertos.cpp sim/brightness_test/runtime_test.cpp -o "$tmp/runtime"
"$tmp/runtime"
c++ -std=c++17 -pthread -Isim/brightness_test/include -Isrc src/brightness_calibration.cpp src/brightness_controller.cpp src/brightness_runtime.cpp sim/brightness_test/runtime_freertos.cpp sim/brightness_test/concurrency_test.cpp -o "$tmp/concurrency"
"$tmp/concurrency"
c++ -std=c++17 -pthread -DHOMEDASH_BRIGHTNESS_REFERENCE_TEST=1 -Isim/brightness_test/include -Isrc src/brightness_calibration.cpp src/brightness_controller.cpp src/brightness_runtime.cpp sim/brightness_test/runtime_freertos.cpp sim/brightness_test/reference_test.cpp -o "$tmp/reference"
"$tmp/reference"
c++ -std=c++17 -pthread -DHOMEDASH_BRIGHTNESS_REFERENCE_TEST=1 -Isim/brightness_test/include -Isrc src/brightness_calibration.cpp src/brightness_controller.cpp src/brightness_runtime.cpp sim/brightness_test/runtime_freertos.cpp sim/brightness_test/reference_concurrency_test.cpp -o "$tmp/reference-concurrency"
"$tmp/reference-concurrency"
c++ -std=c++17 -pthread -Isim/brightness_test/include -Isrc src/brightness_calibration.cpp src/brightness_controller.cpp src/brightness_runtime.cpp sim/brightness_test/runtime_freertos.cpp sim/brightness_test/instance_test.cpp -o "$tmp/instance"
"$tmp/instance"
