# Cardputer ADV hardware validation

Date: 2026-10-14, America/Phoenix. Connected device is ESP32-S3, 8 MB flash, COM6, serial 50:78:7d:cd:fb:98.

## Result

SignalSweep installed through the user's M5Launcher WUI/SD workflow and booted. A USB export allocation defect was found, fixed, and the updated application was flashed only into its verified resident slot. The final SD/WUI container is rebuilt with that same application.

| Test | Result | Evidence |
| --- | --- | --- |
| Full pre-write backup | Passed | 8,388,608 bytes; esptool transfer digest validated; saved SHA-256 checked again before application update |
| M5Launcher installation | Passed by user observation and runtime | User installed WUI/SD container and saw scanner. Live running partition is signal at 0x170000 |
| Screen | Passed functional check | User reports visible scanner; firmware recognizes board 24 / ADV, reports 240 x 135 and advancing rendered-frame count |
| Keyboard | Passed functional check | User confirms Tab/M/V respond; telemetry captured 8 and later 12 physical key events. Injected key tests were not substituted for physical presses |
| Cold boot | Passed by user observation | User unplugged USB and power-cycled; scanner returned normally |
| Speaker | Passed functional check | User corrected initial no-sound report and confirmed sound; then confirmed all five category patterns audible and distinct. Firmware showed speaker_playing=true for categories 0..4 |
| SD CSV logging | Passed synthetic logger test | Actual sdLogWrite wrote clearly marked synthetic row to /SIGSWEEP/LOG00002.CSV. On-device exact tail comparison succeeded; file was 158 bytes |
| USB CSV export and persistence | Passed after fix | Initial 48 KB allocation returned mem. ADV USB pages reduced to 6 KB; after app update/reboot, USB exported all 158 bytes with header and synthetic label intact, done=true / more=false |
| BLE control | Passed | Live NUS CFG returned board=cardputer-adv, sd=1, capture_supported=false, log_max=8192 and both scan flags true |
| Capture disabled | Passed | Live BLE capture START returned explicit disabled error; host tests cover both transports |
| Builds/regression checks | Passed | Final ADV and XIAO tier1 builds; sanitized host ADV tests; whitespace check |

## Backup and installed layout

Private original backup: firmware/backup/cardputer-adv-2026-10-14/before-signalsweep-8mb.bin.
SHA-256: 3f448b492bae81a3d18f9de0a252e23d01f15c1bf520bf38fdcb5b4d37187e1c.
The backup directory is gitignored and has not been mirrored or published.

The pre-install layout had Launcher app0, evilca, nrsuit and a shared spiffs data entry. Live installation retained app0 and coredump but replaced the previous app entries with signal and private ssweepfs. Those earlier apps/data remain in the full backup; they are not currently registered in the live launcher table. No restoration has been performed.

Current app slot: 0x170000, size 0x140000. Private ssweepfs: 0x2b0000, size 0x70000, 448 KB. The package requests 256 KB but Launcher applies its configured data-partition sizing. The tested flash ring is 8192 records, 128 KB, leaving filesystem room for signatures/names/anchors.

The final USB update wrote 1,309,216 bytes at 0x170000 with device hash verification. It did not write bootloader, partition table, NVS, or ssweepfs. The app has only 1504 bytes of current-slot headroom; any larger future USB update must check the live partition size or use Launcher to allocate a larger slot.

## Artifacts

- firmware/.pio/build/cardputer_adv/SignalSweep-cardputer-adv.bin: use for SD/WUI installation. 1,374,752 bytes. SHA-256 147d54348e86c2a3aa94ffa51cc8bfa62596183a1ac019bc49bf881c20b6ccdd.
- Final app SHA-256: cd454932c3dc5ef432a1cb13510c89a8d29f38d1c576e6fddfcd8ecb6241683a.
- The earlier file uploaded to SD predates the export fix; replace it with the final local container for future reinstalls.
- Detailed local evidence under ignored firmware/.pio/adv-validation: status-initial.json, speaker-tests.json, sd-test.json, sd-export-test.json, sd-export-after-fix.json, sd-synthetic-test.csv, status-final.json, installed-partitions.bin/json. These are local evidence; the original backup is separate.

## Limits and final settings

Logging is off at completion, matching its initial state. The only row written by the requested logger test is visibly synthetic; it does not demonstrate detection of a real threat. No coordinates were recorded. No SD formatting or file deletion was performed.

USB was absent after the initial WUI/MSC handoff and returned after the user's full power cycle. Later USB application update and export worked. Serial port opens may reset this firmware; fresh frame counters in one-shot queries do not prove spontaneous crashes or long-term stability.

The optional display frame buffer was released under memory pressure during testing. The UI kept drawing directly, so no camera or framebuffer image is claimed. Runtime snapshots showed roughly 65 KB free heap and minimum 25 KB before export adjustment; final fresh-boot snapshot showed 50,292 bytes free, minimum 49,672 and largest block 38,900. These are short test observations, not a sustained-operation guarantee.

Long soak, RF detection accuracy, headphone output, full-card/hot-removal behavior and full keyboard coverage were not tested. The requested functional screen, keyboard, audible alerts and SD CSV write/readback checks passed.

## Capture-enabled follow-up

The scanner-only acceptance above is historical. Capture is now enabled and
installed in signa1 at 0x320000, enlarged to 2 MB in free space after a refreshed
verified full-flash backup. Existing signal and private filesystem entries are
retained. Real SD and USB captures, a 60-second SD run and three same-boot
early stops passed. See [capture validation](cardputer-adv-capture.md) for exact
counts, backup/artifact hashes and remaining phone/physical-key acceptance.
