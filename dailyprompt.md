# Daily accomplishments

## 2026-10-14, in progress
Coverage includes recorded project turns on 2026-10-14; earlier conversations are unavailable. Sections below preserve milestone order. Updated 2026-10-14T13:46:51-07:00.

- Inspected SignalSweep firmware and current ADV hardware docs.
- Completed compatibility assessment: S3 detector reusable, 5 GHz unavailable, capture buffers require redesign, ADV needs display/keyboard/audio and SD integration.
- Saved docs/cardputer-adv-port.md and initialized project records.
- At the initial assessment milestone, no port had been implemented or flashed. Later sections record completed implementation and acceptance.

Evidence: [assessment](docs/cardputer-adv-port.md), [history](logs/conversation-log.md).

### DevKitC boot repair, updated 2026-10-14T03:20:12-07:00
- Verified full backup, artifact equality and live XMC flash ID 20:7018.
- Installed board-specific recognition diagnostic in first 16 KB only; image integrity and full readback passed.
- Verified SignalSweep BLE connection and CMD:CFG, uptime 73 -> 86 seconds, BLE/Wi-Fi scan enabled.
- Original backup preserved. Normal uploads can revert correction; cold boot, phone app and detector accuracy unverified.
- Evidence: [diagnostic](firmware/diagnostics/bootloader-2026-10-14/README.md), [history](logs/conversation-log.md).

### Persistent upload correction, updated 2026-10-14T03:40:51-07:00
- Enabled guarded XMC correction after bootloader generation and before devkitc_n16r8 upload.
- PlatformIO build, exact golden-image equality, idempotence and refusal checks passed.
- Actual upload command corrected original artifact automatically; unknown image stopped before serial access.
- Current artifact checksum/hash valid. COM8 disappeared, blocking fresh physical upload; no device writes in this turn.
- Evidence: [hook documentation](firmware/tools/README-xmc70.md), [history](logs/conversation-log.md).

### Web app location check, updated 2026-10-14T08:01:13.1020390-07:00
- Verified tracked app source, matching remote and release assembly. No clone needed. Live deployment parity unchecked. Evidence: [history](logs/conversation-log.md).

### Cardputer ADV implementation, updated 2026-10-14 08:34:54 UTC-07:00
- Implemented standalone scanner list/detail/hunt UI, keyboard controls, ES8311 speaker alert backend and optional SD CSV alert logging.
- Added explicit no-PSRAM target, bounded target storage/UI and capture rejection.
- ADV and original XIAO tier1 builds passed. Sanitized host ADV tests and existing attack-detector tests passed.
- Packaged app/factory firmware; no hardware flashed. Screen/audio/SD/RF and launcher installation remain unverified.
- Evidence: [ADV guide](docs/cardputer-adv-port.md), [history](logs/conversation-log.md).

### Cardputer ADV live installation and tests, updated 2026-10-14 09:44:39 UTC-07:00
- Completed full private 8 MB backup before writes, verified digest/hash, and built a dedicated-filesystem M5Launcher SD/WUI package.
- User installed through WUI/SD and confirmed scanner screen, keyboard, cold boot and distinct category sounds. Live SD synthetic CSV write/readback and BLE control passed.
- Found/fixed USB export allocation failure, flashed only existing scanner slot with device hash verification, exported the persisted 158-byte CSV after reboot, and rebuilt the final SD package.
- Final ADV/XIAO builds and sanitized host tests passed. Logging off, capture disabled. Original Launcher retained; prior app entries replaced by initial install and preserved in full backup.
- Future SD reinstalls need the updated local BIN. Long soak, RF accuracy and edge-case storage tests remain unverified. Evidence: [hardware report](docs/cardputer-adv-hardware-validation.md).

### ADV next phase planning, updated 2026-10-14 10:32:48 UTC-07:00
- User reports updated BIN on SD; installation of that SD copy remains unconfirmed.
- Proposed controlled RF validation, one-hour logging soak, recovery/settings checks and slot capacity review. No new hardware tests performed. Capture remains disabled.
- Evidence: [current state](logs/project-state.md), [history](logs/conversation-log.md).

### ADV capture enabled and live tested, updated 2026-10-14 11:06:08 UTC-07:00
- Added bounded internal-RAM SD/USB capture and standalone controls, retaining existing Android USB save/analyze protocol.
- Refreshed verified private 8 MB backup after serial corruption, expanded current signa1 in free space to 2 MB, installed app with device verification and checked full table readback. Other entries/filesystem retained.
- ADV/XIAO builds, sanitized capture lifecycle/format tests and existing regressions passed. Real SD/USB captures and one-minute SD record readback passed with zero drops; three same-boot early stops resumed scanning with stable minimum heap.
- Physical capture keys and Android delivery await user checks. Source/app packet prefixes and shared-radio limits remain. Evidence: [capture guide](docs/cardputer-adv-capture.md), [history](logs/conversation-log.md).

### Scanner source modes and coverage correction, updated 2026-10-14 11:40:24 UTC-07:00
- Built ADV/DevKitC compatibility metadata and foreground Android phone scanner; full app and lifecycle/source tests passed. Debug APK built, signature verified, installed separately and showed17 live observations. Background stop/resume checked.
- Removed 47-rule shortcut, exported complete96 generic defaults. User requires full detector parity; specialized decoders/NetHunter raw adapter remain unfinished.
- Read-only OnePlus6T NetHunter probe verified root and driver-advertised monitor/2.4/5GHz; no radio mode changes. Evidence: [source guide](docs/scanner-sources.md).

### Full NetHunter integration, updated 2026-10-14 12:58:42 -07:00
- Completed shared Android/ESP32/ADV callbacks and all96 generic defaults plus tracker/Flock/OpenDroneID/PwnGrid/attack decoders. Same-input parity passes168 fixtures on actual OnePlusJNI and sanitized hostprofiles.
- Installed signed LocalAPK, verified live2.4/5GHz+BLE, saved/read back PCAP/BLE pairs1132/1017 and868/996. Savedlibrary survives restart/reinstall and Android export chooser opens.
- Fixed root permission ANR, stats crash, adapter edge cases and blocked-pipe cleanup. Final sourcehandoff/Home/force-stop restored Wi-Fi and cleared monitorlock without disablingBluetooth; no auto restart.
- OriginalS3 BLE swap and exact96-rule parity verified, capturecontrol26Wi-Fi/78BLE/0drops and telemetryresume. ADV/devkitc/tier1 builds pass; ADV freshswap and boardUSB rawfiledelivery remain unverified. No freshboardflash.
- Evidence: [integration guide](docs/nethunter-integration.md), [history](logs/conversation-log.md).

### Shared-engine board update in progress, 2026-10-14 13:15:12 -07:00
- Confirmed installed OnePlusAPK exacthash and oldfirmwarebackup integrity.
- Completed fresh8MBADV backup and device-digest-verified scanner update at0x320000; Launcherboot/table unchanged.
- OriginalS3 disconnected beforefullbackup, noflash. ADV USB/capture/swap tests awaituser launch/reconnection. TemporaryWiFiADB enabled for phoneUSB hosttest and mustbeclosed aftertests.
- Evidence: [hardware report](docs/shared-engine-hardware-validation.md).

- User requested hold after Launcher booted wrong OS and reports web-loading new BIN. Runtime/slot unverified; further flashing held, both boards currently absent from PC USB. RawUSB/swap acceptance incomplete. TemporaryWi-FiADB returnedtoUSB.

### Shared-engine hardware acceptance completed, 2026-10-14 13:39:42 -07:00
- Confirmed ADV's actual web-installed shared image at signa3/0x520000. Updated original S3 app-only after complete16MB backup, preserving repaired bootloader/table. All old and new backups retained and hashes rechecked.
- Actual PC/OnePlus USB captures passed on both boards: ADV31/134 and37/132, S322/135 and37/136 Wi-Fi/BLE records; all zero buffer drops, readback formats/counts matched and scanning resumed.
- Fixed external source transport teardown, tested close order, installed signed updated LocalAPK and verified168JNI fixtures on OnePlus. Physical phone/ADV/S3 handoffs passed, including root Wi-Fi release and return to native USB.
- Temporary Wi-Fi debugging closed. Short bench acceptance completed; field/long storage and standalone capture-key coverage remain separate.
- Evidence: [hardware report](docs/shared-engine-hardware-validation.md), [history](logs/conversation-log.md).

- 2026-10-14 13:43:13 -07:00: Confirmed that the final APK is already installed as SignalSweep Local, using the preceding phone pull/hash evidence. No new install performed.

### GitHub development summary, updated 2026-10-14T13:46:51-07:00
- Created [DEVELOPMENT-SUMMARY.md](DEVELOPMENT-SUMMARY.md), explaining why each ADV/S3/Android change was made and its recorded result.
- Cross-checked final test counts, hardware limitations, unresolved work, license and publication decisions; all summary evidence links resolve. Current APK and both firmware hashes match the final hardware report.
- Documentation only. No new physical tests, commit, push or publication. Release destination and public Android signing remain undecided.

### OnePlus location diagnosis
- Confirmed precise permission and system location enabled, GPS starts, no network-location provider exposed. User confirmed indoors; 60-second diagnostic returned no fix.
- Built and installed 60-second fresh one-shot location handling with timeout guidance. Location/source/lifecycle checks pass. Outdoor fix remains pending.
- Evidence: logs/conversation-log.md and app/location-test.js.


### Hosting recommendation, updated 2026-10-14T15:50:22.8760365-07:00
- Verified account mrmcgrain and upstream remote. Recommended personal fork plus Pages; VPS can serve app/dist. No publication or Git changes performed. Local changes still need review/commit/push.
