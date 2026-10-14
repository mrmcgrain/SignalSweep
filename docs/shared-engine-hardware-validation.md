# Shared-engine hardware acceptance, 2026-10-14

Both boards now run verified shared-engine firmware. Actual PC and OnePlus USB recordings passed, as did source handoffs. All firmware backups are retained.

## Firmware and backups

| Device | Running application | Image bytes | Verification |
|---|---|---:|---|
| Cardputer ADV | signa3 at 0x520000, installed by user through M5Launcher WUI | 1,318,848 | Device digest matched shared build |
| Original S3 | app0 at 0x10000, application-only update | 1,074,608 | Device digest matched app; untouched boot region and table separately verified |

The earlier ADV application-only write at signa1/0x320000 preserved its boot region/table. Launcher subsequently booted a different OS; the user installed the new SD container through WUI. We read the resulting table and confirmed the actual running signa3 slot before acceptance. Current table retains Launcher, earlier scanner slots and ssweepfs.

The original S3 identity is 44:bd:8d:69:9e:74, XMC 20:7018, 16 MB flash/8 MB PSRAM. Flashing used COM8 CH343 UART; raw capture uses native USB COM13. Its existing repaired bootloader and operator attack/logging settings were retained.

Fresh backups:

- ADV 8 MB, 16 verified 512 KiB reads: firmware/backup/cardputer-adv-shared-engine-20261014-retry/full-flash.bin, SHA256 2e2aba5d9bcb1ea04ce29ebc0e0c80e881c169874b288ff75fb01092ecde39d9.
- S3 16 MB, 32 verified reads at 921600 baud: firmware/backup/esp32-s3-shared-engine-20261014-retry/full-flash.bin, SHA256 3573cfe970f66496c76e3ed38cdce07ce0003e06d585ddd28747d84f1ded5658.

All original full backups and interrupted attempts remain intact. Earlier S3/ADV attempts stopped when USB disappeared; neither incomplete backup authorized a write. Backup hashes were rechecked at completion. Private manifests/write/verify logs remain inside backup folders and are excluded from Git/mirroring.

## Actual five-second recordings

| Path | Wi-Fi | BLE | Buffer drops | Readback bytes |
|---|---:|---:|---:|---:|
| ADV to PC USB | 31 | 134 | 0 | 20,043 |
| ADV to OnePlus USB | 37 | 132 | 0 | 21,784 |
| S3 to PC USB | 22 | 135 | 0 | 17,662 |
| S3 to OnePlus USB | 37 | 136 | 0 | 21,984 |

Every received/stored SSCAP record decoded with bounded radio/length checks and matched final counters. Both boards resumed live detector telemetry. Phone recordings were written by the actual app to Documents and pulled for independent validation. The installed analyzer also counted the same radio totals.

- ADV phone file: signalsweep-capture-20261014-132401.sscap, SHA256 2c20973db7ba4c0b299814773f16df48c9ec37e62e35be6a7b4a0a4ab18e2271.
- S3 phone file: signalsweep-capture-20261014-133357.sscap, SHA256 e4c6c88b2fb7b6fc5fe22eb494eabb836d40024d1c730e0beda8d011fb479662.

Private captures/reports are in firmware/.pio/adv-capture-validation. Phone radios stayed on for bench debugging/handoffs; this is not a quiet-RF or long-soak benchmark.

## Source handoffs and final APK

The test exposed selectors that stopped phone scanning but left another external transport open. Both USB/BLE selectors now await full disconnect before opening the next link. The actual app VM regression checks close order. Full app/source/lifecycle suites passed; Vite, Capacitor and JDK21 Gradle build ran sequentially.

Final signed Local APK was installed and pulled back, SHA256 35ac7202b980a9b010b0d3257efd8939b449fdb3e30a00529c81e5b9f3d7e52b. All 168 fixtures passed in the installed OnePlus JNI engine.

Physical handoffs passed:

- NetHunter -> ADV BLE: 22 board targets; phone mode0/Wi-Fi1 and no monitor lock.
- Phone API -> ADV USB, and NetHunter -> ADV USB: live board telemetry returned. One reconnect discarded a partial initial telemetry frame; saved capture was complete.
- Final APK ADV USB -> S3 BLE -> ADV USB: 16/45/24 targets respectively; old USB port/listeners closed before BLE, old BLE connections absent after return to USB.
- Final APK S3 USB -> NetHunter -> S3 USB: NetHunter received 399 Wi-Fi frames on 2.4/5 GHz and 1,250 BLE advertisements, then returned to 47 board targets. Phone mode0/Wi-Fi1/Bluetooth1/no lock verified.
- S3's 96 effective rules match phone defaults. Omitted weight0 in firmware readback equals default weight, as the shared engine defines it.

Some initial subscriptions discarded one partial telemetry frame. No continuing parser loss or raw-file count loss was observed. ADV receive-only was restored to its original true setting after the BLE test. S3 settings were retained.

Temporary Wi-Fi ADB was returned to USB mode and TCP port5555 closure verified. Last observed phone source was S3 USB with live telemetry and capture inactive.

## Limits and repeat tools

S3/ADV hear 2.4 GHz and store 256-byte prefixes; NetHunter hears both bands and records full Wi-Fi frames. Equal-input shared decoder tests do not establish equal antennas, sensitivity or field accuracy. Longer storage stress and physical standalone capture-key checks remain separate work. Existing offline analysis is Flock-focused and has outdated C5-only 5GHz wording.

- tools/backup_board.py: MAC/capacity checks, sequential stub-digest-verified reads, complete backup assembly only after all reads pass.
- tools/update_shared_application.py: full backup hash, known slot/image-fit/current table checks, app-only write, device digests for app and preserved boot/table.
- tools/test_cardputer_capture.py --board cardputer-adv or esp32-s3: actual capture/readback/count/scanner-resumption checks; SD requires reported support.

See logs/conversation-log.md for chronology and docs/nethunter-integration.md for phone operation.
