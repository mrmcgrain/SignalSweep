#!/system/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Fixed internal QCACLD adapter. Never interpolate app/user text into root commands.
set -eu
NH=/data/local/nhsystem/kali-arm64
LOCK=/data/local/tmp/signalsweep-monitor.lock
WIFI=$(settings get global wifi_on)
MODE=$(cat /sys/module/wlan/parameters/con_mode)
[ "$MODE" = 0 ] || { echo 'Wi-Fi is already owned by another monitor session' >&2; exit 1; }
[ -x "$NH/usr/bin/tcpdump" ] && [ -x "$NH/usr/sbin/iw" ] || exit 1
mkdir "$LOCK" || { echo 'SignalSweep monitor is already owned' >&2; exit 1; }
chmod 700 "$LOCK"
CAP= HOP= WATCH=
cleanup() {
    trap - EXIT INT TERM HUP
    [ -z "$CAP" ] || kill -INT "$CAP" 2>/dev/null || true
    [ -z "$HOP" ] || kill "$HOP" 2>/dev/null || true
    [ -z "$WATCH" ] || kill "$WATCH" 2>/dev/null || true
    ip link set wlan0 down 2>/dev/null || true
    echo "$MODE" > /sys/module/wlan/parameters/con_mode
    ip link set wlan0 up 2>/dev/null || true
    case "$WIFI" in 1|2) svc wifi enable;; *) svc wifi disable;; esac
    # Never let a stopped client's full stdout pipe delay radio restoration.
    sleep 0.2
    [ -z "$CAP" ] || kill -TERM "$CAP" 2>/dev/null || true
    sleep 0.2
    [ -z "$CAP" ] || kill -KILL "$CAP" 2>/dev/null || true
    [ -z "$CAP" ] || wait "$CAP" 2>/dev/null || true
    rm -f "$LOCK/pid"
    rmdir "$LOCK"
    echo 'SignalSweep Wi-Fi ownership restored' >&2
}
trap cleanup EXIT INT TERM HUP
echo $$ > "$LOCK/pid"
OWNER=$$
exec 3<&0
# A closed app pipe or missed heartbeats restores Wi-Fi, including app force-stop.
(trap - EXIT INT TERM HUP; while IFS= read -r -t 10 beat <&3; do [ "$beat" = alive ] || break; done; kill -TERM "$OWNER") & WATCH=$!
svc wifi disable
sleep 2
ip link set wlan0 down
echo 4 > /sys/module/wlan/parameters/con_mode
sleep 1
ip link set wlan0 up
chroot "$NH" /usr/sbin/iw dev wlan0 set channel 6
# Use only enabled frequencies advertised by this device's driver, avoiding DFS.
FREQS=$(chroot "$NH" /usr/sbin/iw phy phy0 info | awk '/MHz \[/ && !/disabled|radar detection/ {print $2}')
(trap - EXIT INT TERM HUP; while :; do for freq in $FREQS; do case "$freq" in *[!0-9]*|'') continue;; esac; chroot "$NH" /usr/sbin/iw dev wlan0 set freq "$freq" 2>/dev/null || true; sleep 0.28; done; done) & HOP=$!
chroot "$NH" /usr/bin/tcpdump -Z root -i wlan0 -s 0 -U -w - & CAP=$!
wait "$CAP"
