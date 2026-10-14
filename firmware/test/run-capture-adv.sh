#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
g++ -std=c++17 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined \
    -DSIGNALSWEEP_CARDPUTER_ADV=1 -DSIGNALSWEEP_CAPTURE_HOST_TEST=1 -Istubs \
    capture_adv_test.cpp ../src/mode_capture_adv.cpp -o "$build_dir/capture_adv_test"
"$build_dir/capture_adv_test" "$build_dir/test.SSC"
python3 - "$build_dir/test.SSC" <<'PY'
import base64, struct, sys
with open(sys.argv[1]) as f:
    lines = f.read().splitlines()
records = [base64.b64decode(s, validate=True) for s in lines if not s.startswith('#')]
assert len(records) == 34
wifi = [r for r in records if r[0] == 0]
ble = [r for r in records if r[0] == 1]
assert len(wifi) == 33 and len(ble) == 1
assert struct.unpack_from('<I',wifi[-1],1)[0] == 40 # eight dropped packets
assert all(len(r) == 15+struct.unpack_from('<H',r,13)[0] for r in records)
assert ble[0][15:] == bytes([1,2,3,4,5,6,1,2,1,6])
print('Saved SSCAP base64 records independently decoded and validated')
PY
