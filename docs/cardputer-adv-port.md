# Cardputer ADV standalone scanner

Implemented 2026-10-14. ADV build and host tests passed. Full connected-device backup and functional WUI/SD hardware validation completed. See cardputer-adv-hardware-validation.md for evidence and limits. The previous XIAO tier1 build also passed.

## Features

- Standalone 240 x 135 scanner list and target detail pages, with RSSI hunt meter and loss/staleness indication.
- TCA8418 keyboard via pinned M5Cardputer 1.1.1 / M5Unified 0.2.26 / M5GFX 0.2.33.
- Existing category alert rhythms and hunt clicks through the ADV ES8311 speaker driver, with persistent mute and three volume settings. Plugging in headphones changes the physical audio output through the hardware jack detection.
- Optional SD alert CSV logging using the existing logger and privacy rules. The SD slot uses SCK40, MISO39, MOSI14, CS12 on Arduino FSPI / SPI2. M5GFX uses SPI3 for the LCD, so SD retry does not reconfigure the display bus.
- Battery estimate through M5 Power, BLE/USB companion control, receive-only control and radio on/off controls.
- Packet capture now uses bounded internal-RAM queues with SD and USB destinations. A standalone capture page and the existing Android companion support start/stop and visible drops. CMD:CFG reports capture_supported=true. See [capture guide](cardputer-adv-capture.md) for limits and pending hardware acceptance.

## Keyboard

| Key | Action |
| --- | --- |
| K / J, or ; / . | Previous / next target |
| Enter | Open detail / return to list |
| Tab | Settings / list |
| Backspace | Return to list |
| H | Start/stop hunting selected MAC, from list or detail |
| X | Clear hunt, including a missing target |
| M | Toggle persistent sound mute |
| A | Toggle matches-only / all signals |
| L | Toggle persistent alert logging |
| V | Cycle persistent volume 1 through 3 |
| R | Remount/retry SD from the foreground loop |
| Q | Toggle receive-only advertising state |
| B / W | Toggle BLE / Wi-Fi scanning, respectively |
| C | Open capture page; return to list after capture |
| D on capture page | Cycle 15 / 60 / 300 seconds / until stopped |
| Enter on capture page | Start SD capture / stop and save |
| Backspace on capture page | Stop and return to list |
| G0 short tap | Existing bounded advertising window when receive-only |
| G0 held five seconds | Existing factory reset |

Controls use press transitions so held keys do not repeatedly toggle settings. Target selection follows its MAC when RSSI ordering changes. A missing selection returns to the list; the hunt stays armed until X/H clears it. Sentinel flood summaries cannot be hunted. B/W pauses are runtime controls and are not saved across reboot.

## Logging

Logging is off by default. Press L to enable it, insert a FAT-compatible card and press R if the card was inserted after boot. Settings report SD state; the main status explicitly shows LOG:OFF or the card state while logging is enabled. Without a working card, the existing flash alert log still records eligible events and settings show FLASH ONLY.

CSV files are /SIGSWEEP/LOGnnnnn.CSV. Logging follows existing eligible alert events rather than recording every observation. Category suppression, ignore rules and hunt suppression still apply. No coordinates are written. UTC fields remain blank until a USB/BLE host supplies time; records still have boot/seconds ordering. SD writes run on the periodic task, outside radio callbacks. Card failure disables card writes until a successful retry. Packet capture is a separate explicitly started workflow described in the capture guide.

## Memory and radio limits

The stock target is built as 8 MB flash with no PSRAM, native USB CDC and DIO flash. It does not inherit the local DevKitC memory or bootloader overrides. The detector table is bounded to 64 entries on ADV, preferring higher-score matches and older equal-score entries for replacement while protecting pending logs. Dense environments can exceed that limit; this is not an exhaustive radio inventory.

The UI displays at most the 24 strongest eligible rows. Companion reports are capped at 12 matches / 8 all-signal rows per push and keep the existing round-robin behavior. UI snapshots do not move the companion report cursor. The optional 8-bit frame buffer costs about 32 KB; UI drawing falls back to direct drawing if allocation fails or free heap falls below 40 KB.

The ADV filesystem mounts only ssweepfs, never the shared spiffs label. The source layout declares a 256 KB data partition and an 8192-record flash ring, leaving room for signatures, names and time anchors. The SD CSV logger does not use that ring-size limit.

Only 2.4 GHz Wi-Fi is supported. Existing S3 BLE Remote ID limits remain, including the disabled extended-advertising configuration. Wi-Fi/BLE share one RF module; no second radio or GPS is added. No detection accuracy or reception-range claims were established by compilation.

## Build and package

From firmware/:

~~~powershell
& C:\Users\micha\.platformio\penv\Scripts\pio.exe run -e cardputer_adv
& C:\Users\micha\.platformio\penv\Scripts\python.exe tools\package_cardputer.py
~~~

Packaging is offline and does not open a serial port. Outputs are inside the ignored firmware/.pio/build/cardputer_adv directory:

- SignalSweep-cardputer-adv.bin: first-install M5Launcher SD/WUI container. Contains the app plus partition table declaring ssweepfs. Upload to the SD file browser and choose Install. Launcher extracts the app and creates its private data partition without writing the embedded bootloader.
- SignalSweep-cardputer-adv-app.bin: application only. It requires an already-existing ssweepfs partition; use the container above for the first installation.
- SignalSweep-cardputer-adv-factory.bin: merged bootloader, partition table, OTA initialization and application. Direct-flash offset 0x0. Direct USB flashing replaces the existing launcher bootloader/partition layout. Do not use this file for direct flash when preserving Launcher; choose the SD container instead.
- SignalSweep-cardputer-adv-manifest.json: artifact hashes, source hashes, offsets, roles and a pointer to the live hardware validation report.

Identify the exact board and preserve a private backup before a later physical flash. Do not commit or mirror backups. The package helper does not flash. The hardware validation turn updated only the verified live application slot after its full backup.

## Verification

- PlatformIO cardputer_adv: passed, static RAM 89,352 / 327,680 bytes, program 1,308,849 / 2,621,440 bytes.
- PlatformIO tier1: passed, preserving XIAO build compatibility.
- test/run-cardputer.sh: passed with address and undefined-behavior sanitizers. Covers bounded admission, pending-log protection, MAC-stable selection, held/same-count key changes, clock rollover and USB/BLE capture rejection using the actual disabled capture source.
- Existing test/run.sh attack-detector tests: passed.
- Source whitespace check: passed.
- esptool application checksum and validation hash: valid. Packaged artifact hashes, application offset and partition bytes: verified.

Remaining follow-up checks: prolonged operation and heap, real RF detection accuracy, headphones, full-card/removal and remount behavior, wider keyboard and hunt coverage. Requested functional acceptance is recorded in the hardware report.

## Primary hardware references

- [M5Stack Cardputer ADV pin map](https://docs.m5stack.com/en/core/Cardputer-Adv)
- [M5Cardputer source](https://github.com/m5stack/M5Cardputer)
- [Espressif ESP32-S3 radio specifications](https://www.espressif.com/en/products/socs/esp32-s3)

## Physical test session, 2026-10-14

- Full 8 MB COM6 backup completed before writes; checksum file is local and private under firmware/backup/cardputer-adv-2026-10-14. Backup transfer digest and file SHA-256 verified.
- Bootloader and protected Launcher app bytes match known M5Launcher 2.9.1. Two installed apps are present. The live installation replaced the earlier app entries; they are preserved in the full backup. Launcher itself remained installed.
- Rebuilt SD container uses 256 KB ssweepfs and explicitly mounts that label, avoiding formatting another application filesystem. Package parser metadata and embedded application match verified.
- Foreground USB diagnostics: CMD:ADV:STATUS reports board, display geometry, frame count, physical key-event count, I2S state, heap and SD state. CMD:ADV:SCREEN exports the rendered buffer only; it is not camera evidence of the actual LCD. CMD:ADV:SPEAKER:0 through :4 requests each category pattern and obeys mute. CMD:ADV:SDTEST writes one clearly marked synthetic alert through the real CSV logger and checks its readback. CMD:ADV:REBOOT performs a software restart.
- WUI/SD installation and functional screen/keyboard/speaker/SD tests passed. USB export needed a 6 KB ADV page fix, installed and retested. See [hardware validation](cardputer-adv-hardware-validation.md).

Scanner-only hardware milestone: logging remained off, capture was compiled out. User subsequently reported the updated SD copy and completion of follow-up validation. A newer capture-enabled build is now prepared; see the capture guide. Hardware capture acceptance remains pending.
