# Project State

## Objective
SignalSweep detects BLE/Wi-Fi signatures, trackers, Remote ID and optional attack indicators. Cardputer ADV has a standalone scanner, speaker alerts and SD logging/capture. Android supports external boards and phone-only scanning.

## Architecture
Shared detector callbacks/types/weights feed ESP32 BLE NUS/USB and Android JNI. The companion shows the selected source. NetHunter temporarily owns internal QCACLD monitor mode alongside Android BLE. Explicit recording saves full Wi-Fi PCAP and raw BLE JSONL. Boards save SSCAP with 256-byte packet prefixes via USB/SD. Ordinary Android API scanning has limited AP observations.

## Important Files
- DEVELOPMENT-SUMMARY.md: public-facing development summary with reasons, results, evidence links and outstanding publication decisions.
- firmware/src/detector/: common BLE/Wi-Fi callbacks, weights and target types.
- firmware/src/mode_watchers_watch.cpp/.h: rules and shared detector integration.
- firmware/src/mode_capture.cpp, mode_capture_adv.cpp and capture_buffer.h: PSRAM and bounded ADV capture.
- firmware/src/cardputer_ui.cpp/.h: display, keyboard, speaker and diagnostics.
- firmware/src/sd_store.cpp: storage; preserve pre-existing user changes.
- firmware/platformio.ini and ignored devkitc.local.ini: build environments and N16R8 setup.
- firmware/tools/package_cardputer.py: M5Launcher SD container.
- firmware/tools/backup_board.py, update_shared_application.py and test_cardputer_capture.py: guarded backup/update and hardware acceptance.
- app/public/app.js, scan-sources.js and native-rules.json: source UI and 96 defaults.
- app/android/app/src/main/cpp/: shared detector JNI and observation adapter.
- app/android/app/src/main/java/com/signalsweep/app/: native scanner, root supervisor and PCAP reader.
- app/android/app/src/main/assets/nethunter-capture.sh: fixed monitor commands and heartbeat cleanup.
- docs/nethunter-integration.md and shared-engine-hardware-validation.md: operation and evidence.
- logs/prompt-log.md: exact visible transcript since 2026-10-14; earlier history unavailable.

## Services and Environment
Existing repository G:\esp32\signalSweep\SignalSweep on Windows/PowerShell. PlatformIO Espressif32 6.13.0, Arduino/NimBLE/ArduinoJson; Vite/Capacitor. Android builds use JDK 21, NDK 30.0.15729638 and CMake 4.1.2. Physical OnePlus 6T ONEPLUS_A6013 runs Android 15/kernel 4.9.337+ with NetHunter at /data/local/nhsystem/kali-arm64. The com.signalsweep.app.local package preserves the separate existing release/data. USB serial and BLE NUS carry board telemetry. Upstream website deployment parity is unverified.

## Decisions
- Phone, S3 and ADV compile the same callbacks and 96 generic defaults, plus specialized trackers, Flock, OpenDroneID, PwnGrid and attack windows. Equal inputs use the same engine; bands, sensitivity, PHYs and packet prefixes differ.
- S3/ADV hear 2.4 GHz; NetHunter phone reception is verified on 2.4/5 GHz. BLE recordings contain advertisements/scan responses.
- Capture is explicitly authorized. No Wi-Fi injection. Stop/background/force-stop/source changes restore phone Wi-Fi ownership and clear the monitor lock. No automatic scan restart.
- Source selection closes the old USB/BLE transport before opening another. Recording starts only by explicit action.
- Preserve Launcher, filesystems, NVS and the repaired S3 bootloader. Application-only writes follow complete private backups. Commit and push to the personal fork authorized on 2026-10-14 for preservation and Pages setup; upstream publication is not authorized.

## Known Issues
- OnePlus location indoor test: services and precise permission enabled; GNSS starts but returned no fix after 60 seconds. ROM exposes no network-location provider. Outdoor physical-fix acceptance remains pending.
- Field accuracy across device brands and prolonged/full-card/removal tests remain unverified. Five-second capture acceptance is not a soak test.
- Some initial reconnects discard one partial telemetry frame. Complete capture files matched counts; no continuing parser loss was observed.
- Existing offline SSCAP analysis focuses on Flock. Its help text incorrectly says only C5 hears 5 GHz; NetHunter also supports it. Correct in a future APK update.
- Standalone capture-key acceptance remains unconfirmed. Earlier ADV screen, keyboard, speaker and SD logging checks passed.
- Preserve the local N16R8 XMC 20:7018 bootloader family-0x70 correction and devkitc.local.ini hook. Unknown bootloader images stop normal uploads.

## Current Work
Location handling updated on 2026-10-14: fresh one-shot requests allow 60 seconds, preserve the 50 m pin threshold and show timeout guidance. Local APK built/installed/launched, SHA256 c300413d21f15faa850ce89238de1c0294e17cf906b5719c1ef523d883cec0c1. Location, source and lifecycle regressions pass. This supersedes the older APK hash below; firmware unchanged.
GitHub-ready development summary prepared locally on 2026-10-14. Personal fork mrmcgrain/SignalSweep and companion GitHub Pages publication authorized 2026-10-14; setup completed and live at https://mrmcgrain.github.io/SignalSweep/app/. Firmware release/version choice and public Android signing remain undecided. Current local APK and firmware hashes match the final hardware report.
Requested shared-engine updates, PC/OnePlus USB raw capture and physical source swaps completed 2026-10-14 13:39:00 -07:00. Last observed phone connection was S3 USB with live telemetry and no active capture. Phone Wi-Fi/Bluetooth were normal with no monitor lock. Temporary Wi-Fi ADB was disabled and port 5555 closure verified. Final Local APK installed and pulled hash matched 35ac7202b980a9b010b0d3257efd8939b449fdb3e30a00529c81e5b9f3d7e52b.

## TODO
- Optional field/longer/storage-edge checks and physical standalone capture keys.
- Correct the legacy analyzer's 5 GHz wording.

## Last Known Working State
- ADV web-installed signa3 at 0x520000, 1344 KiB slot: 1,318,848-byte shared image, SHA256 b7e163910f018b139baf8e05338dd00131f31609376a841323bf7e95ec3fbfda, device digest verified. Earlier signa1 at 0x320000/2 MB and ssweepfs at 0x2b0000 retained. Our application write preserved boot/table; user web install later changed the layout, whose current readback is saved.
- S3 app0 at 0x10000: 1,074,608 bytes, SHA256 95b131edf16aabf27586b0ea2e56978af6e68cedd370cfbd0dc04f85cff8eb07, device digest verified. Boot region and partition table separately verified unchanged. Existing attack/logging settings retained. Native USB/PSRAM capture works.
- PC USB: ADV 31 Wi-Fi/134 BLE; S3 22/135. Direct OnePlus USB: ADV 37/132; S3 37/136. All zero buffer drops, every stored record validated, scanners resumed.
- Final APK: 168/168 JNI fixtures and app/source/lifecycle regressions pass. Physical ADV USB -> S3 BLE -> ADV USB closes old links. NetHunter -> ADV BLE/USB and S3 USB restores mode0/Wi-Fi1/Bluetooth1/no lock. Final S3 handoff followed 399 Wi-Fi/1,250 BLE observations across both Wi-Fi bands. S3's 96 effective rules match phone defaults.
- ADV, devkitc_n16r8 and tier1 builds, shared sanitized fixtures and ADV capture sanitizer tests passed earlier. Firmware images were unchanged by the final APK transport fix.

## Backups and Recovery
All private full backups and partial attempts remain under firmware/backup, excluded from Git and Obsidian. Current hashes reverified:
- Fresh ADV 8 MB: cardputer-adv-shared-engine-20261014-retry/full-flash.bin, SHA256 2e2aba5d9bcb1ea04ce29ebc0e0c80e881c169874b288ff75fb01092ecde39d9.
- Fresh S3 16 MB: esp32-s3-shared-engine-20261014-retry/full-flash.bin, SHA256 3573cfe970f66496c76e3ed38cdce07ce0003e06d585ddd28747d84f1ded5658.
- Original ADV before SignalSweep: 3f448b492bae81a3d18f9de0a252e23d01f15c1bf520bf38fdcb5b4d37187e1c.
- ADV before capture: c1fcd10f5e19345ba89a38135cff0f44da8724c63110d6fb5b7688f0f83ffd7c.
- Original S3 16 MB: 744a9efa572d7184f74f167479971ae3369d88b99b3838141b3494935d0ff4c6.
Private update evidence stays local. XMC rollback: firmware/diagnostics/bootloader-2026-10-14/README.md; hook usage: firmware/tools/README-xmc70.md.

## Records and Mirror
Project files are authoritative. Mirror F:\Obsidian\SecondBrain\01-Projects\SignalSweep uses hash-verifying project-records/project-recaps helpers. No backups, raw captures or secrets are mirrored. Canonical prompt log, history, state, changelog and daily/weekly/index files are maintained.


## Personal fork and Pages
- Existing project stays at G:\esp32\signalSweep\SignalSweep. origin now points to https://github.com/mrmcgrain/SignalSweep.git; upstream retains https://github.com/B0z0dcl0wn/SignalSweep.git. The existing fork matched base 45a1351 before publication.
- Pages uses GitHub Actions. Dedicated pages.yml builds the companion at /SignalSweep/app/ with root redirect and deployment.json. Original combined release workflow restricted to upstream.
- Before publication, 259 changed/untracked/local-config files copied with SHA-256 verification into .git/codex-preservation/20261014-155203. Existing source/diagnostic bytes unchanged; no reset, clean, stash or reclone.
- firmware/diagnostics remains private and ignored alongside backups/local config/signing files. No public APK or firmware release prepared in this turn.
- Local selftest, source, lifecycle and location checks and Vite build passed. Initial selftest from app/ failed on relative firmware path; root invocation passed.
- npm audit reports seven existing advisories, including Capacitor Android critical plus build/native tooling dependencies. No dependency changes made. Pages deployment 38650781044 succeeded for source commit 918d2187611d7be55a6c9817b1bac55435577141. HTTPS root redirect, deployment metadata and app assets verified. Desktop scanner chooser and mobile Settings rendered with zero browser console errors; mobile width/scrollWidth both 390. No new hardware connection test performed.

- Source changes committed/pushed to personal main in 918d218; follow-up hosting records committed separately. main tracks origin/main. Repository-local Git identity uses verified mrmcgrain account and GitHub noreply email.
- Hosting guide: docs/github-pages.md. Repository homepage points to the companion. Screenshot artifacts are private/ignored under output/playwright; verification browser closed.
