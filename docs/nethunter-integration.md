# NetHunter integration

Implemented and tested on 2026-10-14 on the USB-connected OnePlus 6T ONEPLUS_A6013, Android 15, kernel 4.9.337+.

## Shared detection engine

Firmware and Android compile the same `firmware/src/detector/weights.inc`, `types.inc`, `ble.inc` and `wifi.inc`. Android supplies a bounded Arduino/NimBLE observation facade. It does not maintain a separate JavaScript classifier. All 96 generic defaults are exported by `app/tools/export-native-rules.py`.

The shared engine includes:

- Generic AND conditions, accumulated confidence, explicit weights and exact UUID matching.
- Apple offline Find My trackers, Tile, Samsung SmartTag and Google Find Hub/FMDN. Apple short status pings and Eddystone remain negative.
- Legacy/modern Flock Penguin advertisements, SSIDs, OUI/wildcard probes and exact IE fingerprints.
- OpenDroneID Basic ID, Location, System, Operator ID, Self ID and packed messages through BLE, Wi-Fi vendor IEs and NAN. The reference C library is shared.
- PwnGrid/Pwnagotchi JSON and gzip reassembly.
- Deauth, Karma, BLE popup spam and tracker floods using shared attack-window functions. Attack rules retain the default-off toggle.
- Sticky target details, cross-radio corroboration, and common list/alert thresholds.

Adapter fixtures distinguish advertised UUID lists from service-data UUIDs and preserve complete-name priority. Android 15 supplies real BLE address types. Older Android versions leave unknown addresses unclassified for popup-spam decisions. Short Remote ID payloads are bounded on every source.

## Radio and capture limits

| Source | Wi-Fi input | Raw recording | Engine |
|---|---|---|---|
| OnePlus NetHunter | Internal monitor mode, enabled non-DFS 2.4/5 GHz frequencies | Full Wi-Fi frames in PCAP and raw BLE JSONL | Shared callbacks |
| ESP32-S3 | 2.4 GHz | Existing PSRAM SSCAP backend, 256-byte packet prefixes | Shared callbacks |
| Cardputer ADV | 2.4 GHz | Bounded internal-RAM queues to SD/USB SSCAP, 256-byte prefixes | Shared callbacks |
| Normal Android phone scan | Android AP observations and BLE advertisements | Observation snapshot, no raw Wi-Fi capture | Shared callbacks with limited Android inputs |

Equal decoder behavior is tested for equal inputs. Bands, sensitivity, hop timing, Bluetooth PHYs, target capacity and packet-prefix limits remain source-specific. The S3 radio cannot reproduce the phone's extra 5 GHz reception. BLE recording contains received advertisements and scan responses, not interception of arbitrary Bluetooth connections. Android requests legacy and supported extended advertisements/PHYs.

## Phone operation

1. Open **SignalSweep Local**. The installed SignalSweep release and its data remain separate.
2. Choose **Connect → NetHunter raw scan**. Grant this app root access in Magisk and Android scanning permissions if requested.
3. Scanning uses internal QCACLD `wlan0` and Android BLE. The app holds the screen on while scanning. Switching apps, locking the phone or pressing Stop ends scanning.
4. In **Survey**, choose a duration and start capture. Zero means until stopped. Stopping recording leaves the live scanner running.
5. Survey also lists saved captures after app restart. Tap a saved pair to export it. The saved panel shows the PCAP/BLE paths and **Export PCAP and BLE log**. Export opens Android's chooser and sends nothing automatically.
6. Stop/disconnect scanning to restore Wi-Fi. Returning to the app never restarts scanning automatically.

Files live under `/storage/emulated/0/Android/data/com.signalsweep.app.local/files/captures/`. Writes begin only after explicit capture start. Live state is bounded and cleared at stop. Wi-Fi recording includes data frames; detection consumes management frames. Malformed radiotap driver records remain in the PCAP and are counted/skipped for detection. Kernel drops are unknown while the stream continues; the UI does not claim zero.

## Root ownership

The adapter targets the verified `/data/local/nhsystem/kali-arm64` chroot and QCACLD `/sys/module/wlan/parameters/con_mode`. It uses fixed commands, `tcpdump -Z root`, and driver-reported frequencies. It performs no Wi-Fi injection.

Root permission is obtained before startup. The supervisor saves Wi-Fi service/mode, releases Android Wi-Fi before monitor mode, and holds an exclusive lock. A heartbeat pipe restores Wi-Fi when the app stops or dies. Root waits run off the UI thread. Bluetooth stays with Android. Unsupported tool/kernel setups fail visibly.

## Verification

- 168 fixtures pass in sanitized host phone, S3 and ADV profiles and in the installed OnePlus APK through JNI.
- Fixtures cover all 96 generic rules, missing conditions, gates, trackers, false positives, Flock, JSON/gzip PwnGrid, attack windows, tracker flood, timer wrap, UTF-8, split advertisements, Remote ID streams/packs/NAN, IDs and positions.
- Radiotap tests cover endian order, alignment, RSSI, frequency/channel, FCS, malformed records and oversized-record rejection. Initial eight-second monitor test received 78 packets with zero kernel drops.
- A 15-second app recording saved/read back 1,132 PCAP records, 292,657 bytes, and 1,017 valid BLE records, 63,054 payload bytes. BLE address types included public and random.
- Final UI-driven 12-second capture saved 868 Wi-Fi frames, 164,570 bytes, and 996 BLE advertisements. The completed panel displayed matching counts and the export control.
- Live reception covered channels 1–11 and enabled non-DFS 5 GHz channels. Stop restored mode 0, Wi-Fi enabled and Bluetooth enabled, with no monitor lock.
- Initial permission-prompt ANR and stats-map crash were fixed. The app-death test restored Wi-Fi. Final lifecycle retests are recorded in project state.
- ADV, DevKitC N16R8 and upstream tier1 builds pass, along with ADV capture sanitizer tests and app selftests.
- Final APK phone-to-BLE handoff, Home/background stop and force-stop each restored mode 0, Wi-Fi enabled, Bluetooth enabled and no monitor lock. Resume/relaunch showed no active source or observations. Fixed a blocked stdout cleanup race by draining the pipe and restoring the radio before bounded tcpdump termination.
- Original ESP32-S3 physically connected through BLE on the final APK, received 176 telemetry messages with zero parser drops, and its 96-rule table exactly matched phone defaults. A three-second board capture control test reported 26 Wi-Fi and 78 BLE records with zero buffer drops, then telemetry resumed. Board raw payload transport requires USB or SD; BLE counters do not prove raw-file delivery.
- Saved capture library retained two pairs through reinstall/relaunch, rendered two export controls and opened the actual Android chooser. The chooser was cancelled without sending.
- Subsequent ADV physical acceptance passed: web-installed shared image device-digest matched at signa3/0x520000, PCUSB31Wi-Fi/134BLE and directOnePlusUSB37Wi-Fi/132BLE saved/readback with zero bufferdrops. NetHunterRAW-to-ADV BLE and USB handoffs restoredmode0/Wi-Fi1/noLock. See shared-engine-hardware-validation.md for current originalS3 update status. Fixtures prove software behavior, not field accuracy for every device brand.

## Build and repeat checks

From the repository root:

```text
python app/tools/export-native-rules.py
python app/tools/make-decoder-fixtures.py
node app/selftest.js
node app/source-test.js
node app/native-lifecycle-test.js
wsl bash firmware/test/run-shared-detector.sh
```

Android uses JDK 21, NDK 30.0.15729638 and CMake 4.1.2. CMake uses installed ArduinoJson headers or fetches upstream v7.4.3. Run Vite build, Capacitor sync, then Gradle assembleDebug sequentially.

`app/tools/PcapReaderTest.java` compiles with the actual Android reader and can validate saved PCAPs. Debug-only `PhoneScanner.replayFixtures()` returns JNI pass/failure totals and refuses replay during scanning. Private captures/evidence remain in ignored `firmware/.pio/adv-capture-validation/`.

The M5Launcher SD container is `firmware/.pio/build/cardputer_adv/SignalSweep-cardputer-adv-capture.bin`. Install it through Launcher. The app-only BIN requires the existing partition layout. Earlier private full firmware backups are retained.

## Completed hardware acceptance

Both boards now have verified shared-engine images and actual PC/OnePlus USB raw recordings. Final app closes old external transports before opening a new source. The installed APK hash is35ac7202b980a9b010b0d3257efd8939b449fdb3e30a00529c81e5b9f3d7e52b and168JNI fixtures pass. See [hardware report](shared-engine-hardware-validation.md) for counts, source handoffs, backup identities and limits. Temporary Wi-Fi ADB was disabled after testing.
