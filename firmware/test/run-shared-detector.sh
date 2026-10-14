#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
fw=firmware/src
json=firmware/.pio/libdeps/tier1/ArduinoJson/src
for file in opendroneid odid_wifi vendor/miniz_tinfl; do
  gcc -O1 -g -fsanitize=address,undefined -DSIGNALSWEEP_PORTABLE=1 -I"$fw" -c "$fw/$file.c" -o "$tmp/$(basename "$file").o"
done
for profile in phone esp32_s3 cardputer_adv; do
  flags=(); [ "$profile" != cardputer_adv ] || flags+=(-DSIGNALSWEEP_CARDPUTER_ADV=1)
  g++ -std=c++17 -O1 -g -fsanitize=address,undefined "${flags[@]}" -I"$fw" -I"$json" firmware/test/shared_detector_test.cpp "$tmp"/*.o -o "$tmp/$profile"
  "$tmp/$profile" app/android/app/src/main/assets/decoder-fixtures.json
done
