#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -DSIGNALSWEEP_NO_CAPTURE=1 -Istubs cardputer_port_test.cpp ../src/mode_capture.cpp -o "$build_dir/cardputer_port_test"
"$build_dir/cardputer_port_test"
