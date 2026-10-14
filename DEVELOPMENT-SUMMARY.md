# SignalSweep development summary

Work completed on October 14, 2026. Prepared for a future GitHub publication; repository destination, release numbering and download links are still undecided.

We extended the existing SignalSweep project into a scanner that can run on a Cardputer ADV, the original ESP32-S3 board, or an Android phone. The updated Android companion can switch between external boards and phone scanning, including a tested NetHunter raw-capture mode on a rooted OnePlus 6T.

The main result is a shared detection engine across these sources, working standalone ADV controls and recording, and verified capture delivery to both a computer and the OnePlus. This document explains the changes, their reasons and their measured results.

## Where we started

SignalSweep already had ESP32 firmware, a browser/Android companion, detector rules and capture protocols. We reused those components. The original project and its earlier releases predate this work; this summary covers our recorded changes and tests, rather than claiming we created every existing feature.

The starting goals were to support the Cardputer ADV, keep the original ESP32-S3 working, and make one companion app usable with either board or with the phone alone. Those goals exposed several differences in memory, radio access, storage and connection ownership that needed explicit handling.

## 1. Checked the hardware and reused the existing app source

**Why we did it.** Sharing an ESP32-S3 processor does not make two boards interchangeable. The ADV has its own screen, keyboard, audio and SD wiring, and this target cannot use the original board's PSRAM capture design. We also needed to know whether the hosted companion source was already available locally.

**What changed.** We assessed the ADV hardware, documented the required adaptations and confirmed that the repository already contained the companion source and release assembly workflow. A second clone was unnecessary.

**Result.** Development used the existing checkout and app. The hardware assessment established the limits before implementation. Both S3 boards receive 2.4 GHz Wi-Fi; adding a phone does not give their radios 5 GHz support. Parity with the currently hosted website was not verified.

## 2. Repaired the original ESP32-S3 boot problem

**Why we did it.** The original DevKitC N16R8 board was stuck in a reset loop. Its XMC flash chip reported ID `20:7018`, which did not match the recognition check in the bootloader image being used.

**What changed.** After checking the original full backup and live chip identity, we applied a board-specific recognition correction in the first 16 KB boot region. We then added a PlatformIO hook that applies the same correction after bootloader generation and before upload. Exact-image hash checks reject unknown images instead of applying the patch blindly.

**Result.** Boot-region readback matched the repaired image, the reset loop stopped, and BLE configuration queries showed increasing uptime with scanning enabled. Build-hook tests verified correct output, repeatability and rejection of invalid images. The later shared-engine application update preserved the repaired bootloader and partition table, with device verification of both.

This repair is specific to the verified bootloader and flash-chip combination. A new framework or bootloader image requires review before the hook can accept it.

## 3. Built a standalone Cardputer ADV scanner

**Why we did it.** The ADV should work as a handheld scanner without needing the phone for every action. It also needed to fit within internal RAM and use the board's actual peripherals.

**What changed.** We added an explicit ADV build target, scanner list/detail/hunt/settings screens, keyboard navigation, battery status, ES8311 speaker alerts and optional SD alert CSV logging. Target storage and UI snapshots were bounded. Display and SD use separate SPI buses. The package uses a private `ssweepfs` filesystem so installation does not format a shared application filesystem.

**Result.** ADV and the existing tier1 builds passed, along with sanitized host tests. During installation and bench checks, the user confirmed the screen, physical keyboard, cold boot and five distinct alert sounds. A synthetic CSV was written to SD, read back and exported after reboot.

A real USB export failed because its original 48 KB allocation was too large for the ADV. Reducing the export page to 6 KB fixed the failure, and the persisted CSV then exported successfully. Headphone operation and prolonged storage stress remain unverified.

## 4. Enabled recording within the ADV's memory limits

**Why we did it.** The first ADV port deliberately disabled packet capture while the scanner and peripherals were validated. Once recording was requested, copying the original PSRAM design would not work on this target.

**What changed.** We added bounded internal-RAM queues with 32 Wi-Fi slots and 8 BLE slots. Radio callbacks copy small records; foreground code drains them to SD or USB. The UI includes recording controls, status, errors and drop counters. Capture pauses the normal detector while it owns the radio callbacks, then restores scanning and the operator's enabled-radio settings.

**Result.** Real SD and USB captures decoded correctly, including a 60-second SD test containing 259 Wi-Fi records and 1,693 BLE records. That file contained 200,759 bytes and reported zero buffer drops. Repeated early stops also saved valid records and resumed scanning. Sanitized tests exercised queue overflow, stop/cancel paths, storage failures, timer rollover and recovery.

Both board capture formats store up to 256 bytes of each packet, with length and radio metadata. Queue overflow remains possible during heavier traffic. BLE recordings contain received advertisements and scan responses, not arbitrary Bluetooth connection traffic. The phone adds storage and analysis but cannot remove the board's radio or buffer limits. Physical standalone capture-key acceptance remains outstanding.

## 5. Made one Android app support multiple scanner sources

**Why we did it.** A separate app for each board would duplicate work and make source switching harder. Phone-only scanning also needed to be available when no board was connected.

**What changed.** We added source capability reporting, foreground Android BLE/Wi-Fi scanning and source selection for external USB/BLE devices. A separate `SignalSweep Local` package allowed testing without replacing the existing release package or its data.

**Result.** The phone-only fallback acquired live observations and stopped when backgrounded. Returning to the app did not silently restart it. Ordinary Android scanning has limited Wi-Fi access through access-point observations; it cannot supply the raw frames required by every detector. That limitation led to the NetHunter integration.

## 6. Shared the full detection engine across phone and boards

**Why we did it.** An initial 47-rule phone export reduced coverage and did not meet the requirement to preserve detection behavior. Matching the generic rule count alone would also leave specialized decoders inconsistent.

**What changed.** We exported all 96 generic defaults and compiled the same detector callbacks, target types and weights into Android through JNI and into the board firmware. Shared processing includes tracker signatures, Flock signatures, OpenDroneID, PwnGrid/Pwnagotchi and optional attack-window indicators. Attack indicators retain their default-off setting. Adapter fixes aligned UUID, service-data, device-name and address-type handling.

**Result.** All 168 detector fixtures passed in sanitized host profiles for phone, S3 and ADV, and through JNI in the installed OnePlus APK. The original S3's 96 effective rules also matched the phone defaults. Fixtures cover positive cases, false positives, malformed inputs, protocol decoding and state/timing behavior.

Equal inputs now use the same engine. Actual reception still depends on each source's bands, antenna, sensitivity, channel hopping and available packet data. These tests establish decoder behavior, not field accuracy for every device brand.

## 7. Added NetHunter raw scanning and recording

**Why we did it.** The rooted OnePlus 6T had monitor-capable Wi-Fi and a NetHunter environment. Using that hardware could supply raw Wi-Fi input and 5 GHz reception beyond the S3 boards' capabilities.

**What changed.** We integrated the tested QCACLD monitor-mode path with the NetHunter chroot and Android BLE scanning. Explicit recording saves full Wi-Fi frames in PCAP and BLE advertisements in JSONL. The app includes a saved-capture library and Android export chooser.

Root supervision saves and restores Wi-Fi state, holds an exclusive monitor lock and uses heartbeat cleanup. Stop, backgrounding, force-stop and source changes release monitor ownership. Returning to the app requires a new scan action. The integration performs no Wi-Fi injection.

**Result.** On the physical OnePlus 6T running Android 15, scanning received 2.4 GHz and enabled non-DFS 5 GHz traffic alongside BLE. A 15-second recording saved and independently validated 1,132 Wi-Fi records and 1,017 BLE records. A later 12-second UI-driven capture saved 868 Wi-Fi frames and 996 BLE advertisements. Saved pairs survived reinstall/relaunch, and export opened Android's chooser.

We fixed a root-permission UI freeze, a statistics serialization crash and a blocked-pipe cleanup race. Final lifecycle tests restored normal Wi-Fi mode, kept Bluetooth enabled and cleared the monitor lock. Malformed driver records remain in the PCAP but are skipped for detection. Ongoing kernel packet-loss totals are unknown, so board zero-drop results should not be attributed to NetHunter.

This integration was physically verified on this OnePlus setup. Compatibility with other rooted phones, kernels or NetHunter layouts remains unverified.

## 8. Preserved recovery paths during installation

**Why we did it.** Firmware updates can overwrite a launcher, filesystem, settings or a working boot repair. Serial disconnections also made a completed-looking backup unreliable without read verification.

**What changed.** We retained original backups and created fresh full 8 MB ADV and 16 MB S3 backups using verified chunks. Update tooling checks board identity, capacity, backup hash, current partition layout and image fit before application writes. SD/WUI packaging supports M5Launcher installation.

An earlier ADV slot was too small for the growing capture image. After checking free space and overlap, we expanded that slot to 2 MB while preserving other entries. A later user WUI install selected a different slot, so acceptance checked the actual running layout rather than assuming the old address.

**Result.** The final ADV shared image was verified in `signa3` at `0x520000`. The S3 application was verified at `0x10000`, with its repaired bootloader and partition table unchanged. Original, fresh and interrupted backup attempts were retained. Private full-flash backups and raw captures stay outside Git and Obsidian.

The initial ADV installation replaced previous application entries. Their contents remain in the original full backup; we did not restore them or claim that installation preserved every earlier app entry.

## 9. Verified real USB delivery and source switching

**Why we did it.** A capture counter on a board does not prove the phone received a usable file. Source switching also exposed an app bug that left the previous USB or BLE connection open.

**What changed.** The app now waits for complete external-transport disconnection before opening the next source. Regression tests check that order. We then tested both boards through computer USB and direct OnePlus USB, saved files through the actual companion and independently decoded every stored record.

**Result.** Final five-second captures passed:

| Connection | Wi-Fi records | BLE records | Reported buffer drops |
|---|---:|---:|---:|
| Cardputer ADV to computer USB | 31 | 134 | 0 |
| Cardputer ADV to OnePlus USB | 37 | 132 | 0 |
| Original ESP32-S3 to computer USB | 22 | 135 | 0 |
| Original ESP32-S3 to OnePlus USB | 37 | 136 | 0 |

Saved record counts and formats matched, the phone analyzer reported the same totals, and both boards resumed scanning. Physical ADV USB to S3 BLE to ADV USB switching passed. NetHunter-to-board handoffs restored normal phone Wi-Fi ownership. The final NetHunter-to-S3 USB test followed 399 Wi-Fi observations across both bands and 1,250 BLE advertisements.

Some reconnects discarded one initial partial telemetry frame. No continuing parser loss or capture-file count loss was observed in these tests. The final signed Local APK was installed and pulled back from the phone; its hash matched the final build. Temporary Wi-Fi debugging was disabled and its TCP port closure verified.

## 10. Saved build, test and recovery documentation

**Why we did it.** Future development needs the reasons and evidence behind these changes, including limits and recovery instructions.

**What changed.** We added hardware guides, capture/source documentation, repeatable backup/update tools and regression fixtures. Project state, chronological history, exact visible transcripts and daily/weekly recaps are stored locally and mirrored with hash verification into the documented Obsidian project folder. Recorded history begins October 14, 2026; earlier conversations are unavailable.

**Result.** The project has a documented path for rebuilding, repeating checks and identifying what was actually verified. Application source and test evidence remain authoritative when older milestone notes describe work that was still pending at that time.

## Current status and publication work

The requested shared-engine implementation and short bench acceptance are complete. The physical ADV and original S3 run verified updated firmware, and the updated Local APK is installed on the OnePlus. ADV, DevKitC N16R8 and tier1 builds passed in the recorded implementation work, along with app/source/lifecycle checks, sanitized capture tests and the 168 detector fixtures.

Outstanding work includes longer field and storage tests, full-card/removal behavior, physical standalone capture-key acceptance and broader device accuracy/compatibility checks. The offline analyzer remains Flock-focused and has outdated help text claiming only C5 supports 5 GHz; the tested NetHunter source also receives it. An earlier dependency audit reported seven pre-existing vulnerabilities; this work did not resolve or re-audit them.

The new versions have not been published as part of this work. Once a destination is chosen, publication still needs a deliberate release/version decision, artifact selection and download links. The installed Local APK is a debug build, so public Android signing and distribution also need a decision. Publication must retain the upstream project's GPL-3.0 license and attribution requirements. Private flash backups, captures and device-specific evidence are excluded from the public package.

## Supporting documents

- [Final shared-engine hardware acceptance](docs/shared-engine-hardware-validation.md)
- [NetHunter integration and repeat checks](docs/nethunter-integration.md)
- [Scanner source capabilities](docs/scanner-sources.md)
- [Cardputer ADV port](docs/cardputer-adv-port.md)
- [Cardputer ADV hardware validation](docs/cardputer-adv-hardware-validation.md)
- [ADV capture design](docs/cardputer-adv-capture.md)
- [Guarded XMC bootloader hook](firmware/tools/README-xmc70.md)

Some design and milestone documents retain earlier pending-test statements. The final shared-engine hardware acceptance report records the completed USB and source-handoff tests summarized here.
