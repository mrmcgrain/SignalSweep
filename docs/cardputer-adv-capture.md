# Cardputer ADV capture and Android companion

Capture implementation added 2026-10-14. Build and host lifecycle tests pass.
Capture firmware is installed. Five-second real SD and computer USB capture passed; Android phone delivery and physical capture-key acceptance are pending.

## Two destinations

Connect the ADV to the Android phone with a USB-C data cable. In the installed
SignalSweep companion, connect using USB and start an environment capture with
the SD destination unchecked. The firmware emits the existing `CAP:` records;
the app saves a `.sscap` file in its Documents storage and runs its existing
on-phone analyzer. Keep the app foreground and the phone screen awake. USB has
one host at a time, so disconnect the computer before connecting the phone.
Installed phone version compatibility is not yet physically verified.

For standalone use, press C on the ADV to open the capture screen. Press D to
cycle 15 seconds, 60 seconds, 300 seconds, or until stopped. Enter starts to SD,
then stops an active capture early. Backspace stops and returns to the list.
Files are `/SIGSWEEP/CAPnnnnn.SSC`. C returns to the list after capture finishes.
Counts, remaining time, filename, errors and queue drops appear on screen.
SD capture can also be started from the companion over BLE; the card holds the
raw records, and the app's SD download workflow retrieves them afterwards.

USB/BLE commands retain the existing protocol:

```
CMD:CAP:START:60:SD
CMD:CAP:START:60
CMD:CAP:STOP
CMD:ADV:STATUS
```

The second command streams to USB. Duration is 0 through 3600 seconds, with 0
meaning until stopped. Malformed durations are rejected. Capture starts only
after an explicit command or Enter press; it does not resume on reboot.

## Memory and reception

The ADV backend has 32 Wi-Fi queue slots and 8 BLE slots in internal RAM,
about 11 KB combined, plus a 2 KB SD staging buffer. It does not allocate PSRAM
or create capture tasks. It releases the optional display framebuffer before
starting. Radio callbacks only copy bounded records under a short critical
section. Foreground code drains queues, writes/streams records and hops Wi-Fi
channels 1 through 11 every 250 ms.

This follows Espressif's guidance to defer work from the Wi-Fi callback:
[ESP32-S3 Wi-Fi driver documentation](https://docs.espressif.com/projects/esp-idf/en/release-v4.4/esp32s3/api-guides/wifi.html).
Only Wi-Fi management, data and control payloads are copied. Miscellaneous
packets have no usable payload and are excluded. Frames with reception errors
are excluded. BLE scan is passive and stores advertisements, not connections.
NUS advertising follows existing receive-only settings; capture does not make
the whole device silent automatically.

Wi-Fi records retain at most the first 256 bytes. Original length, captured
length, radio, sequence, microsecond timestamp, channel and RSSI retain the
SSCAP v1 format. BLE records retain the existing MAC/type/advertisement format.
Encrypted Wi-Fi bodies remain encrypted. This is not full-length lossless
capture, Bluetooth connection sniffing or 5 GHz capture.

The queues drop incoming packets when full. Sequence numbers advance on dropped
packets, and counters report the number dropped. Capture status counts accepted
Wi-Fi/BLE records separately from drops. On successful SD completion, a comment
footer preserves accepted, dropped and emitted record counts. The `written`
counter counts records encoded to the staging buffer or forwarded to USB; it
does not prove phone delivery, and SD errors can leave those records uncommitted.
SD data syncs about once per second; the latest second can be lost on power cut.

Detection pauses during capture because the modes share radio callbacks.
The backend waits for detector tasks to exit before starting, then restores
detection and the operator's BLE/Wi-Fi enabled flags on completion or failure.
Radio and identity changes are rejected while capture owns the radios. SD
failure ends capture, closes its file, reports an error without success and
restores scanning. Existing alert CSV logging resumes with detection.

The Android phone supplies file storage, analysis and a larger UI. It cannot
add PSRAM or radio bands to the ADV, or eliminate losses during bursts.

## Installation and evidence

Use the freshly packaged `SignalSweep-cardputer-adv.bin` via M5Launcher SD/WUI.
The older scanner slots were 0x140000 bytes, too small for this build. The
current signa1 slot at 0x320000 has now been expanded to 2 MB, without moving
other entries or ssweepfs. For another installation, an allocation of at least
0x150000 bytes is required; 2 MB allows future growth.
Do not USB-write the new app at the old address into that undersized slot.
Launcher can select/create a larger app slot and handle required data partitions:
[Launcher install layout documentation](https://github.com/bmorcelli/Launcher/wiki/Explaining-the-project).
Back up the app's `ssweepfs` before accepting a layout change because moving or
formatting that data partition can lose signatures, names and flash alert history.
SD files are separate. Keep the private full-flash backup off Git and Obsidian.

Validation completed:

- ADV build and original XIAO tier1 build.
- Actual ADV backend with host fakes under AddressSanitizer and UBSan: pending
  start cancellation, busy rejection, queue overflow/reuse, USB output, manual
  and timed stops, timer rollover, no card, SD write/sync failures, failed radio
  setup, detector shutdown timeout and later recovery, and scan-flag restoration.
- Saved test SSCAP decoded independently in Python, validating every record's
  length, eight-drop sequence gap and BLE payload bytes.
- Existing bounded scanner tests and capture analyzer self-test.

Live hardware results so far: SD 5 s saved and read back 19 Wi-Fi and 163 BLE
records in CAP00001.SSC, zero drops; computer USB 5 s received 22 Wi-Fi and 152
BLE records, zero drops. Exact record lengths/counts and detector telemetry
resumption passed. Post-capture heap was 63,868 / 64,448 bytes, minimum heap
37,088 / 37,328 bytes respectively. These are short tests.

Additional live results: 60 s SD capture saved and read back 259 Wi-Fi and
1,693 BLE records, zero drops, 200,759 bytes in CAP00002.SSC. Detector resumed,
heap 62,752 bytes and minimum 39,240 bytes. Three early-stop SD captures in the
same boot completed successfully and resumed detector telemetry, with heap
64,140 / 64,156 / 64,136 bytes and a stable 39,032-byte minimum. The full updated
partition table was read back and exactly matched the resize artifact.

Still needed: physical capture-key acceptance, Android USB permission/connect/
save/analyze flow and longer capture stress or physical SD failure coverage.
Host fakes are not reception or physical storage proof.

For connected-device validation after installation, run from firmware/:

```powershell
& C:\Users\micha\.platformio\penv\Scripts\python.exe tools/test_cardputer_capture.py --destination sd --seconds 5
& C:\Users\micha\.platformio\penv\Scripts\python.exe tools/test_cardputer_capture.py --destination usb --seconds 5
```

The runner verifies board/capture capability before starting, validates raw
record lengths and final counts, checks that capture ended and detector
telemetry resumed, and keeps raw RF evidence in ignored `.pio/adv-capture-validation`.
It makes no flash or partition writes. USB computer capture is a transport test,
not proof of Android phone delivery.

## Installed capture image and recovery

The app BIN is 1,318,592 bytes, SHA256
`b0b9082e8cb168949681c888fcd78440c1bcad93e87170c0906009a14544ccb3`.
The Launcher SD container and capture alias are 1,384,128 bytes, SHA256
`d15ce5b539907246cf82a325ab66c47af525ff39081706a2307f76df6ec12b38`.

Before the update, a fresh full 8 MB private backup captured the live scanner
installation. Two long reads failed with corrupt serial data; sixteen 512 KB
reads each passed esptool digest verification and assembled a complete dump.
SHA256 `c1fcd10f5e19345ba89a38135cff0f44da8724c63110d6fb5b7688f0f83ffd7c`.
It stays under ignored firmware/backup/cardputer-adv-2026-10-14 and is not mirrored.

Preflight verified protected bootloader/Launcher equality, current scanner app
hash, exact live table equality to backup, free-space bounds and all preserved
entry types/labels/offsets/sizes. Only signa1 size changed from 0x140000 to
0x200000, plus table MD5. USB write affected table sector 0x8000 and application
0x320000 only; esptool verified both writes. Original signal app at 0x170000,
ssweepfs at 0x2b0000, Launcher and other partition entries remain. The original
pre-scanner dump is retained separately for earlier removed apps.

Evidence files stay private under ignored firmware/.pio/adv-capture-validation.
