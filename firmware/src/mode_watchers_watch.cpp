// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 B0z0dcl0wn and the SignalSweep contributors

#include "mode_watchers_watch.h"
#include "ble_serial.h"
#include <NimBLEDevice.h>
#include <NimBLEScan.h>
#include <NimBLEAdvertisedDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
// The S3 ROM exports tinfl_decompress (esp32s3.rom.ld), so we gunzip a compressed
// pwngrid advertisement for free. The C5 ROM (IDF 5.x) dropped miniz, so there is
// no rom/miniz.h there.
#if defined(CONFIG_IDF_TARGET_ESP32C5)
  // Vendor the MIT tinfl (inflate only) -- one-way compatible into GPL-3.0,
  // header intact. Same symbols the S3 gets from ROM, so the gzip code below
  // is identical on both boards.
  #include "vendor/miniz_tinfl.h"
  #define PWN_HAS_GUNZIP 1
#else
  #include "rom/miniz.h"
  #define PWN_HAS_GUNZIP 1
#endif
#include "alert_log.h"
#include "sd_log.h"
#include "crash_report.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <vector>
#include "attack_detectors.h"
#ifdef SIGNALSWEEP_CARDPUTER_ADV
#include "scanner_view.h"
#endif
#include <algorithm>
#include "hardware_manager.h"
#include "capabilities.h"
#include "mode_manager.h"
#include "c5_radio.h"
extern "C" {
#include "opendroneid.h"
#include "odid_wifi.h"
}

#if CONFIG_IDF_TARGET_ESP32C5
// NimBLE-Arduino 2.x hands scan results out as const; 1.x (the S3) does not.
// A macro rather than a typedef so the S3's preprocessed source stays
// token-identical (the S3 byte-identical gate).
#define SWEEP_ADV const NimBLEAdvertisedDevice
#else
#define SWEEP_ADV NimBLEAdvertisedDevice
#endif

static const char *TAG = "WatchersWatch";
static const char *SIG_FILE_PATH = "/data/signatures.json";

// Bump whenever the DEFAULT rule set changes meaningfully. The file lives on
// LittleFS and survives a firmware flash, so without this an already-deployed
// device keeps its old rules for ever — which is how the purged Espressif /
// unassigned / locally-administered OUIs would have quietly stayed in service
// on every board that had already booted once.
//   v2: removed a4:cf:12 + 3c:71:bf (Espressif, i.e. this very board's vendor
//       block), cc:cc:cc (unassigned), 82:6b:f2 (locally-administered).
//   v3: removed mfg_id 0x01 ("Flock XUNTONG"). 0x0001 is Nokia's assigned
//       Bluetooth Company ID; on the bench it matched Govee bulbs.
//   v4: added Tracker category (Tile / Samsung SmartTag service UUIDs). AirTag
//       "offline finding" is matched in code, not here (needs a payload type
//       byte the rule schema can't express). Feeds the Tracker buzzer word.
//   v5: removed the Raven rules — device names "raven"/"penguin" and service
//       UUIDs 3100/3200/3300/3400/3500. Both scored at or above CONF_ALERT_MIN
//       (W_NAME/W_UUID = 70), so a single one could fire the buzzer on its own,
//       and both are far too generic to carry that: "raven" and "penguin" are
//       ordinary product words matched as substrings, and a four-hex-digit UUID
//       is substring-matched against every UUID a device advertises. Same
//       failure shape as the mfg 0x01 rule that labelled Govee bulbs "Flock".
//   v6: added 14:b5:cd (upstream's 2026-07-16 addition) and removed f8:a2:d6,
//       which @NitekryDPaul field-demoted in that same revision — it hits a Sony
//       Media Player, not a Flock device. Same class of false positive as the
//       Espressif prefixes purged at v2 and the mfg 0x01 rule purged at v3.
//   v7: removed the standalone mfg 0x09C8 (XUNTONG) rule. At W_MFG it did not
//       beep alone, but it listed any 0x09C8 device as Flock. The Flock battery
//       ("Penguin") is now matched in code (blePenguin), which demands 0x09C8
//       PLUS a serial, or the full "Penguin-<10 digits>" name.
//   v8: label-only OUI hints for Genetec (AutoVu/Sharp ALPR) and Ubicquia
//       (streetlight camera/sensor nodes). OUI-only, so they never beep.
//   v9: SquachWatch-CYD extraction (registry-checked). Registered-maker OUIs
//       (b4:1e:52 Flock, d4:11:d6 ShotSpotter, 00:25:df Axon) list at 60
//       without beeping; Axon body-cam pairing SSIDs; smart glasses route to
//       body cam -- Ray-Ban/Oakley Meta requires Luxottica 0x0D53 AND fd5f
//       together (either alone also appears on other hardware), Snap 0x03C2
//       alone is fine; label-only
//       Motorola Solutions / Verkada / Avigilon Alta / Axis; the three
//       "Sierra" OUIs were Fiberblaze, Bitworks and unregistered -- replaced
//       with the six blocks the IEEE gives Sierra Wireless.
//   v10: Attack gear (SquachWatch-CYD values, registry-checked): Flipper Zero
//        service UUIDs 3081/3082/3083 (80), Flipper Devices company ID
//        0x0E29 and OUI 0c:fa:22 (70), a "flipper" name (60, listed only),
//        the STM32WB-derived 80:e1:26/27 addresses a Flipper Zero uses (60),
//        Pineapple_ management AP SSID (70) and the ESP deauther's "pwned"
//        SSID (60). All "Hacking gear", matched only while Attack is on.
//   v11: Flock's solar battery GATT services as exact 128-bit UUIDs (80):
//        auth e8ccbb38-... and safety 20c944c1-..., from the Flock firmware
//        dump. The first 128-bit UUID rules in the defaults.
//   v12: OUI-Spy's mode-selector AP, SSID prefix "oui-spy" (70, Hacking
//        gear). Its MAC is random per boot, so the default name is the only
//        handle; a renamed board is not caught.
//   v13: OUI-Spy's mode APs: "snoopuntothem" (Detector), "foxhunter"
//        (Foxhunter), "ouispy-" (PCAP + BLE Sniff), all 70, Hacking gear.
//   v14: Axon OUI 00:25:df raised 60 -> 70, so it beeps alone. Axon gear is
//        police gear; two of its 5 GHz APs at a traffic stop stayed silent.
//   v15: same rule for Flock's registered block b4:1e:52 and SoundThinking
//        d4:11:d6 (60 -> 70): no consumer owns either company's hardware.
//   v16: same test for the registry hints. Beep (70): Genetec (now "Genetec
//        ALPR"), Neology, WatchGuard Video, Digital Ally. List only (60):
//        Motorola Solutions (+2 prefixes), Verkada, Avigilon Alta, Axis
//        (+2), Ubicquia. Fleet routers stay at 30.
#define SIG_SCHEMA_VERSION 16

static SemaphoreHandle_t watchersMutex = NULL;
static bool watchersRunning = false;
static std::vector<WatcherSignature> loadedSignatures;
static std::vector<WatcherTargetInfo> trackedTargets;

static WatcherTargetInfo* appendTrackedTarget(const WatcherTargetInfo& target) {
#ifdef SIGNALSWEEP_CARDPUTER_ADV
    // Keep at most 64 radio targets; protect pending records during eviction.
    size_t slot = scanner_view::admissionSlot(trackedTargets, target, millis(), 64);
    if (slot == SIZE_MAX) return nullptr;
    if (slot < trackedTargets.size()) {
        trackedTargets[slot] = target;
        return &trackedTargets[slot];
    }
#endif
    trackedTargets.push_back(target);
    return &trackedTargets.back();
}
static TaskHandle_t watchersTaskHandle = NULL;

// Hunt: the one opt-in behaviour change on an otherwise always-on detector.
// While a MAC is hunted, its RSSI drives the Geiger clicker so you can walk a
// planted tracker down by ear. Everything else keeps detecting and beeping
// normally — this is a single variable, not a mode: no queue, no NVS, no
// selector, none of the state machine the last pass deleted.
// Persisted. The whole point of hunting is to unplug the board from the laptop,
// put it on a USB battery, and walk away from the phone — which is a power
// cycle. A lock that evaporated on reboot could only ever be used while
// tethered to the thing you were trying to walk away from.
static String huntMac = "";
// Ring is requested from the BLE write callback but performed on the 1 Hz task:
// a GATT connect must not run on the NimBLE callback stack, and it has to stop
// the scan first.
static String pendingRingMac = "";

// Foxhunt / "filter off": report every tracked device, not just signature
// matches. This is the old Beacon Bandit behaviour — turn the filter off, find
// something interesting, lock it, walk it down — and it is the one case where
// the reported list deliberately ignores CONF_LIST_MIN.
//
// It must never touch the ALERT gate. CONF_ALERT_MIN still governs the buzzer,
// so foxhunting shows you every phone in the room without beeping at any of
// them. Reporting everything is cheap; beeping at everything is the failure
// mode this whole design exists to avoid.
//
// Persisted, like everything else the operator sets. The extra payload only
// costs anything while a phone is subscribed — sendBleSerial() returns early
// with no client — so the earlier "always boot quiet" argument was protecting
// a budget that isn't being spent when nobody is listening.
static bool scanAll = false;
// Attack-gear detection (pwnagotchi, and later deauth/karma). Off by default:
// it is attack-adjacent and, unlike a signature match, some of its detectors
// are rate-based and prone to false alarms in busy places. Persisted, echoed.
static bool attackDetect = false;

// Rules filed under this category (Flipper, Pineapple, deauther SSIDs) are
// matched only while Attack is on; the pwnagotchi detector uses it too.
#define HACKING_GEAR_CATEGORY "Hacking gear"

// True while a rule must be skipped: it is attack gear and the operator has
// Attack off. Read live on every advert/frame, so flipping the toggle takes
// effect immediately. A plain bool read: safe in the radio callbacks.
static inline bool ruleGatedOff(const WatcherSignature& sig) {
    return !attackDetect && sig.category == HACKING_GEAR_CATEGORY;
}

// Per-device Ignore (idea from SquachWatch-CYD). MAC + the AlertCategory it
// sounded as, so the list still says what it was after the device is gone.
// Guarded by watchersMutex: read from noteAlertForTarget() (always called with
// it held), written by the command handlers on the NimBLE host task.
struct IgnoreEntry { uint8_t mac[6]; uint8_t cat; };
static_assert(sizeof(IgnoreEntry) == 7, "IgnoreEntry must be 7 bytes -- the NVS blob length check in restoreWatchersState() depends on it");
static IgnoreEntry ignoreList[IGNORE_MAX];
static uint8_t ignoreCount = 0;

// Caller holds watchersMutex. Bytes already parsed -- no re-parse per check.
static bool isIgnoredBytesLocked(const uint8_t m[6]) {
    for (uint8_t i = 0; i < ignoreCount; i++)
        if (memcmp(ignoreList[i].mac, m, 6) == 0) return true;
    return false;
}

// Caller holds watchersMutex.
static bool isIgnoredLocked(const String& mac) {
    if (ignoreCount == 0) return false;
    uint8_t m[6];
    if (!alertLogParseMac(mac.c_str(), m)) return false;
    return isIgnoredBytesLocked(m);
}

// Whether the clicker is currently sounding. Tracked separately from huntMac so
// the hunt can stay armed while the target is out of earshot.
static bool huntAudible = false;

// Hunting a Wi-Fi device needs two things the BLE path gets for free.
//
// First, the RSSI cannot be handed to the clicker from the promiscuous
// callback: updateGeigerRssi() takes the hardware mutex, and that callback must
// never block. So the sample is parked in a volatile here and the 1 Hz task
// applies it.
//
// Second, and more importantly, the channel hopper means we only hear a given
// access point while parked on its channel -- roughly one dwell in thirteen.
// That is fine for sweeping and useless for walking a signal down, so while a
// Wi-Fi target is being hunted the hopper stops and stays on its channel.
static volatile int huntWifiRssi = 0;
static volatile uint32_t huntWifiRssiMs = 0;
static volatile int huntChannel = 0;
#if CONFIG_IDF_TARGET_ESP32C5
// Which Wi-Fi band(s) the hopper covers (SweepBand). Written by setBand() on the
// command path, read by the hop task each dwell -- a byte, so volatile suffices.
static volatile uint8_t sweepBand = BAND_BOTH;
uint8_t getBand() { return sweepBand; }
#endif
// Set by the promiscuous callback on any Remote ID frame; the hopper reads and
// clears it to grant that channel one extra dwell.
static volatile bool odidHeard = false;

// Freshest RSSI for the hunted target, from whichever radio heard it. Written
// from both callbacks (volatile only -- the promiscuous handler must not take a
// mutex) and read by the 4 Hz hunt frame, which is what keeps the phone's meter
// in step with the clicker instead of a second behind it.
static volatile int      huntLastRssi = 0;
static volatile uint32_t huntLastRssiMs = 0;
/**
 * @brief Ensure /data/signatures.json exists on LittleFS, creating default rules if missing
 */
static void ensureSignaturesFileExists() {
    // Regenerate a file written by an older default rule set.
    if (LittleFS.exists(SIG_FILE_PATH)) {
        int fileVersion = 0;
        File existing = LittleFS.open(SIG_FILE_PATH, "r");
        if (existing) {
            JsonDocument probe;
            if (!deserializeJson(probe, existing)) {
                fileVersion = probe["version"] | 0;
            }
            existing.close();
        }
        if (fileVersion < SIG_SCHEMA_VERSION) {
            ESP_LOGW(TAG, "signatures.json is v%d, defaults are v%d — regenerating.",
                     fileVersion, SIG_SCHEMA_VERSION);
            LittleFS.remove(SIG_FILE_PATH);
        }
    }

    if (!LittleFS.exists(SIG_FILE_PATH)) {
        ESP_LOGI(TAG, "%s not found. Creating default signatures file...", SIG_FILE_PATH);
        if (!LittleFS.exists("/data")) {
            LittleFS.mkdir("/data");
        }
        File file = LittleFS.open(SIG_FILE_PATH, "w");
        if (file) {
            JsonDocument doc;
            doc["version"] = SIG_SCHEMA_VERSION;
            JsonArray sigs = doc["signatures"].to<JsonArray>();

            auto addRule = [&](const char* name, const char* cat, const char* oui, const char* mfg,
                               const char* dev, const char* uuid, int weight = 0, const char* ssid = "") {
                JsonObject s = sigs.add<JsonObject>();
                s["name"] = name;
                s["category"] = cat;
                s["oui"] = oui;
                s["mfg_id"] = mfg;
                s["device_name"] = dev;
                s["service_uuid"] = uuid;
                if (weight > 0) s["weight"] = weight;
                if (ssid[0]) s["ssid_prefix"] = ssid;
            };

            // Flock Safety OUI prefixes.
            //
            // This list is not ours. It is the promiscuous-mode set compiled by
            // OrdoOuroborous / @NitekryDPaul (https://github.com/nitekry) and
            // circulated as the "GoFlockYourself" extended OUIs. Compiling it
            // meant physically finding cameras and confirming their prefixes.
            // Everything this detector knows about Flock hardware, it knows
            // because of that work. See CREDITS.md.
            //
            // Four entries from that original list are deliberately absent
            // here, because they cannot mean "Flock" and only generate false
            // positives:
            //   a4:cf:12, 3c:71:bf  Espressif — this board's own vendor block,
            //                       so every ESP32 in range matched. Upstream
            //                       itself flags a4:cf:12 as low confidence.
            //   cc:cc:cc            not an assigned OUI at all.
            //   82:6b:f2            locally-administered bit set (0x02) — that
            //                       is a randomized-MAC prefix, i.e. phones.
            //                       Upstream keeps it deliberately (it is
            //                       DeFlockJoplin's 12th camera) and warns
            //                       against filtering it; we cannot, because
            //                       loadWatchersSignatures() rejects
            //                       locally-administered prefixes at load time
            //                       so a pushed rule set can't reintroduce
            //                       them. Accepted cost: we miss that camera.
            //
            // f8:a2:d6 was dropped at v6 — upstream field-demoted it in the
            // 2026-07-16 revision after it was observed hitting a Sony Media
            // Player. 14:b5:cd was added in that same revision.
            // Annotated, not removed (it is the community's published list):
            // b8:35:32 is not in the IEEE registry; 48:27:ea is Samsung.
            const char* flockOuis[] = {
                "70:c9:4e", "3c:91:80", "d8:f3:bc", "80:30:49", "b8:35:32",
                "14:5a:fc", "74:4c:a1", "08:3a:88", "9c:2f:9d", "c0:35:32", "94:08:53",
                "e4:aa:ea", "f4:6a:dd", "24:b2:b9", "00:f4:8d", "d0:39:57",
                "e8:d0:fc", "e0:4f:43", "b8:1e:a4", "70:08:94", "58:8e:81", "ec:1b:bd",
                "58:00:e3", "90:35:ea", "5c:93:a2", "64:6e:69", "48:27:ea", "14:b5:cd",
                "04:0d:84", "1c:34:f1", "38:5b:44", "94:34:69",
                "b4:e3:f9", "f0:82:c0", "e0:0a:f6"
            };
            for (const char* oui : flockOuis) {
                addRule("Flock Safety MAC", "Flock Safety", oui, "", "", "");
            }
            // The one block the IEEE registers to Flock Safety itself. Beeps on
            // its own (70, v15): no consumer owns Flock hardware, so the address
            // alone is enough. The category keeps flockOui true, so the probe
            // path still sees it. (The flockOuis[] loop above stays at W_OUI:
            // those prefixes are shared with consumer Wi-Fi modules.)
            addRule("Flock Safety MAC (registered block)", "Flock Safety", "b4:1e:52", "", "", "", 70);

            // SoundThinking / ShotSpotter: sells only to police and cities, so
            // its registered address beeps alone (70, v15).
            addRule("SoundThinking", "SoundThinking", "d4:11:d6", "", "", "", 70);

            // NOTE: the Raven service-UUID rules (3100/3200/3300/3400/3500)
            // were removed at v5, when service_uuid was substring-matched
            // against every UUID a device advertises, so a four-hex-digit
            // needle hit constantly — and at W_UUID (70) a single hit was
            // enough to beep. service_uuid is exact-matched now (uuidMatches),
            // but a UUID rule still has to be specific enough to stand alone,
            // because that is exactly what CONF_ALERT_MIN lets it do.

            // Flock's solar battery ("Penguin") GATT services, from the Flock
            // camera firmware dump (the Flock-You project, MIT; also in
            // SquachWatch-CYD). Full 128-bit, Flock's own -- exact-matched, so
            // nothing else carries them. The battery only shows these if its
            // link to the camera has dropped (the camera holds it open): 0 in
            // 173 captures so far. A sighting is a Flock pole, so it beeps.
            addRule("Flock battery (auth service)", "Flock Safety", "", "", "",
                    "e8ccbb38-9532-46a8-9fe5-1814df172e6f", 80);
            addRule("Flock battery (safety service)", "Flock Safety", "", "", "",
                    "20c944c1-add2-42d7-a638-967ee9a26ff6", 80);

            // Device Name Keywords. "raven" and "penguin" were dropped at v5:
            // ordinary words, matched as substrings, scoring W_NAME (70) — i.e.
            // self-sufficient to sound the alarm on someone's bluetooth speaker.
            const char* bleNames[] = {"flock", "pigvision", "fs_"};
            for (const char* name : bleNames) {
                addRule("Flock BLE Name", "Flock Safety", "", "", name, "");
            }
            
            // NOTE: a rule matching manufacturer ID 0x01 was removed here.
            // Bluetooth Company ID 0x0001 is Nokia's, not Flock's, and once the
            // signature file actually started loading it immediately labelled
            // Govee smart bulbs as "Flock Safety / Likely" on the bench. Same
            // class of junk as the Espressif OUIs above.

            // Pre-existing SignalSweep Rules
            // 00:25:df is registered to Axon Enterprise. Axon makes more than
            // body cams (Tasers, docks, in-car Fleet, Signal units), so the
            // name says "device" -- but nearly all of it is police gear, so it
            // beeps on the OUI alone (70, v14). Field 2026-10-12: two Axon
            // 5 GHz APs at a traffic stop listed at 60 and stayed silent.
            addRule("Axon Enterprise device", "Axon", "00:25:df", "", "", "", 70);
            // Body cams broadcast these while pairing (Axon's own admin docs).
            // Vendor-documented, so they beep at the default W_WIFI_SSID (80).
            addRule("Axon Body 2 camera (pairing)", "Axon", "", "", "", "", 0, "AB2-");
            addRule("Axon Body 3 camera (pairing)", "Axon", "", "", "", "", 0, "AB3-");
            addRule("Axon Body 4 camera (pairing)", "Axon", "", "", "", "", 0, "AB4-");
            addRule("Axon device hotspot", "Axon", "", "", "", "", 0, "AXON-");
            addRule("Axon Signal System", "Axon", "", "", "", "fe6c");
            addRule("Axon Signal System", "Axon", "", "", "", "fe6d");
            addRule("Cradlepoint Router", "Fleet / Infrastructure", "00:30:44", "", "", "");
            addRule("Peplink Router", "Fleet / Infrastructure", "00:1a:dd", "", "", "");
            // The six MA-L blocks the IEEE registers to "Sierra Wireless, ULC".
            // The old three were Fiberblaze (00:21:b2), Bitworks (00:07:e2) and
            // an unregistered prefix (00:f0:8a).
            addRule("Sierra Wireless Infrastructure", "Fleet / Infrastructure", "00:a0:d5", "", "", "");
            addRule("Sierra Wireless Infrastructure", "Fleet / Infrastructure", "28:a3:31", "", "", "");
            addRule("Sierra Wireless Infrastructure", "Fleet / Infrastructure", "50:13:9d", "", "", "");
            addRule("Sierra Wireless Infrastructure", "Fleet / Infrastructure", "64:ce:6e", "", "", "");
            addRule("Sierra Wireless Infrastructure", "Fleet / Infrastructure", "84:db:2f", "", "", "");
            addRule("Sierra Wireless Infrastructure", "Fleet / Infrastructure", "cc:93:4a", "", "", "");

            // Surveillance vendors from the IEEE registry, mostly NOT yet heard
            // on the air (much of this gear is wired). The test since v16 is
            // "could an ordinary person own this?" (user decision 2026-10-12):
            //   - police/ALPR-only makers beep on the address alone (70);
            //   - business security makers whose beep would also mean a hotel
            //     radio or an office door reader list without beeping (60).
            // A registered prefix costs nothing if it is never heard.
            // Genetec AutoVu/Sharp are plate readers: "ALPR" routes the row to
            // the Cameras tab and the ALPR beep.
            addRule("Genetec (AutoVu / Sharp)", "Genetec ALPR", "00:0a:b1", "", "", "", 70);
            addRule("Genetec (AutoVu / Sharp)", "Genetec ALPR", "00:bf:15", "", "", "", 70);
            addRule("Genetec (AutoVu / Sharp)", "Genetec ALPR", "0c:bf:15", "", "", "", 70);
            // Neology: plate readers and toll/registration RFID readers.
            addRule("Neology (ALPR / tolling)", "Neology ALPR", "00:17:3d", "", "", "", 70);
            // Police in-car and body cameras. WatchGuard *Video* (now Motorola),
            // never WatchGuard Technologies (00:01:21, 00:90:7f) -- that is the
            // firewall company.
            addRule("WatchGuard Video camera", "WatchGuard body cam", "00:1d:96", "", "", "", 70);
            addRule("Digital Ally camera", "Digital Ally body cam", "00:23:bd", "", "", "", 70);

            // Listed, never beeping (60). The categories avoid every routing
            // keyword ("cam" would file Verkada under body cams), so these sit
            // under All only. Motorola Solutions owns Vigilant ALPR and makes
            // police radios, but also sells two-way radios to every hotel and
            // security firm; consumer "Motorola" phones are Motorola Mobility
            // (Lenovo), a different registrant.
            addRule("Ubicquia (streetlight node)", "Ubicquia", "94:7b:be", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "00:04:7d", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "00:18:85", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "00:1f:92", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "4c:cc:34", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "b8:e2:8c", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "10:74:6f", "", "", "", 60);
            addRule("Motorola Solutions device", "Motorola Solutions", "9c:86:2b", "", "", "", 60);
            addRule("Verkada camera", "Verkada", "e0:a7:00", "", "", "", 60);
            addRule("Avigilon Alta device", "Avigilon Alta", "70:1a:d5", "", "", "", 60);
            addRule("Axis Communications camera", "Axis Communications", "00:40:8c", "", "", "", 60);
            addRule("Axis Communications camera", "Axis Communications", "b8:a4:4f", "", "", "", 60);
            addRule("Axis Communications camera", "Axis Communications", "ac:cc:8e", "", "", "", 60);
            addRule("Axis Communications camera", "Axis Communications", "e8:27:25", "", "", "", 60);

            // Smart glasses: a camera on someone's face, counted like a body
            // cam (category keyword "glasses" routes to it on both sides).
            // Meta Platforms' own company IDs are deliberately absent: Quest
            // headsets carry them too. (Don't write their hex values here:
            // selftest.js fails on them anywhere in this block.)
            // Ray-Ban/Oakley Meta requires BOTH signals, not either alone: an
            // upstream detector found each one is a false-positive magnet on
            // its own (Meta's fd5f service UUID is plausibly on Quest headsets
            // too; the Luxottica company ID alone is weak). A rule ANDs every
            // condition it states, so this scores W_MFG+W_UUID (capped) then
            // the rule's own weight 70 -- only when both are present.
            addRule("Ray-Ban / Oakley Meta glasses", "Smart glasses", "", "0x0D53", "", "fd5f", 70);
            addRule("Snap Spectacles", "Smart glasses", "", "0x03C2", "", "", 70);

            // Attack gear. Category HACKING_GEAR_CATEGORY: matched only while
            // Attack is on (ruleGatedOff), All tab only, generic beep.
            // Flipper Zero advertises one 16-bit service UUID per case colour
            // (its firmware's own constants -- there is no registry for them).
            addRule("Flipper Zero (service UUID)", HACKING_GEAR_CATEGORY, "", "", "", "3081", 80);
            addRule("Flipper Zero (service UUID)", HACKING_GEAR_CATEGORY, "", "", "", "3082", 80);
            addRule("Flipper Zero (service UUID)", HACKING_GEAR_CATEGORY, "", "", "", "3083", 80);
            // Registry-checked: SIG company 0x0E29 and IEEE MA-L 0c:fa:22 are
            // both Flipper Devices. 0c:fa:22 is unproven on the air: a Flipper
            // Zero reportedly builds its BLE address from ST's STM32WB
            // derivation (80:E1:26/27...), which is real on a Flipper but shared
            // with other STM32WB hardware -- so those two are listed-only (60)
            // hints named for what they prove, not "Flipper". In no IEEE
            // registry (selftest allowlists them). Marauder's "Flipper"
            // company ID 0x0FBA is a headset maker.
            addRule("STM32WB address (Flipper Zero uses this)", HACKING_GEAR_CATEGORY, "80:e1:26", "", "", "", 60);
            addRule("STM32WB address (Flipper Zero uses this)", HACKING_GEAR_CATEGORY, "80:e1:27", "", "", "", 60);
            addRule("Flipper Devices (company ID)", HACKING_GEAR_CATEGORY, "", "0x0E29", "", "", 70);
            addRule("Flipper Devices (MAC)", HACKING_GEAR_CATEGORY, "0c:fa:22", "", "", "", 70);
            // The default name, but anyone can type it: listed, never beeps
            // alone (a real Flipper also carries the UUID and beeps from that).
            addRule("Name contains Flipper", HACKING_GEAR_CATEGORY, "", "", "flipper", "", 60);
            // Vendor-default control APs. Pineapple_XXXX is specific; "pwned"
            // is also a common joke network name, so it lists without beeping.
            addRule("Pineapple management AP", HACKING_GEAR_CATEGORY, "", "", "", "", 70, "Pineapple_");
            // OUI-Spy (Colonel Panic) boot selector. Default SSID, bench-proven
            // 2026-10-06; the board randomizes its AP MAC every boot.
            addRule("OUI-Spy selector AP", HACKING_GEAR_CATEGORY, "", "", "", "", 70, "oui-spy");
            // Its modes' default APs. Flock-You WiFi and Sky Spy are passive (no
            // AP, no BLE advertising) and cannot be heard at all.
            addRule("OUI-Spy Detector AP", HACKING_GEAR_CATEGORY, "", "", "", "", 70, "snoopuntothem");
            addRule("OUI-Spy Foxhunter AP", HACKING_GEAR_CATEGORY, "", "", "", "", 70, "foxhunter");
            addRule("OUI-Spy capture AP (PCAP / BLE Sniff)", HACKING_GEAR_CATEGORY, "", "", "", "", 70, "ouispy-");
            addRule("'pwned' SSID (ESP deauther default)", HACKING_GEAR_CATEGORY, "", "", "", "", 60, "pwned");

            // Trackers (planted-on-you category). Keyed on service UUID, which
            // the matcher already handles. AirTag is matched in code (its Find
            // My advert carries no service UUID — just Apple mfr data + a type
            // byte the rule schema can't express). Headless can only say "a
            // tracker is near", not "it's following you" — that needed the
            // geospatial history removed for opsec. Still worth the beep.
            addRule("Tile Tracker", "Tracker", "", "", "", "feed");
            addRule("Tile Tracker", "Tracker", "", "", "", "feec");
            addRule("Samsung SmartTag", "Tracker", "", "", "", "fd5a");

            serializeJsonPretty(doc, file);
            file.close();
            ESP_LOGI(TAG, "Default signatures created successfully.");
        } else {
            ESP_LOGE(TAG, "Failed to open %s for writing!", SIG_FILE_PATH);
        }
    }
}

/**
 * @brief Confidence weights per signal type. An OUI prefix is weak (shared
 * across a vendor's whole catalogue and easily randomized); a matching mfg ID
 * is medium; a device-name substring or service UUID is strong. WiFi vendor-IE
 * and SSID weights live in the promiscuous callback.
 * ponytail: tune these against field false-positive rates.
 */
#include "detector/weights.inc"

// Headless alerting.
//
// With no phone connected the buzzer is the entire user interface, so it has to
// work off both radios — the strongest Flock signal there is (an SSID match,
// weight 80) exists only on the Wi-Fi side, and used to make no sound at all
// because the only triggerAlarm() calls lived in the BLE callback.
//
// Detections record the alert here instead of buzzing directly, and the 1 Hz
// task acts on it. Two reasons: the Wi-Fi promiscuous callback must never block
// (triggerAlarm takes a 10 ms mutex), and draining it once per tick naturally
// rate-limits a dense area to one alert per second instead of a continuous
// scream. Highest confidence seen in the interval wins.
//
// NOTE: this is inherently a *signature* alarm. The geospatial "is it bolted
// down" test needs GPS, which lives in the phone until Tier 3, so headless can
// only ever shout about brands it already knows. That's the deal, and it's
// still worth having.
static volatile int pendingAlertConf = 0;
// Buzzer alerts sounded since boot. Read back via CMD:CFG so the headless path
// can be verified after the fact -- see the comment in getAlertCount().
static volatile uint32_t alertsFired = 0;
// Which buzzer "word" the winning signal earns. The strongest single signal in
// the interval sets both the confidence and the category, so the sound matches
// what actually tripped the alarm.
static volatile AlertCategory pendingAlertCat = ALERT_GENERIC;

// Which categories are allowed to make a noise, one bit per AlertCategory
// (1<<ALERT_ALPR .. 1<<ALERT_GENERIC). Default: all of them. Every AirTag in
// traffic tripping the tracker pattern is correct behaviour and still useless to
// listen to, so the operator picks the words worth hearing. A muted category is
// fully silent — no beep, no LED flash, and it does not count in getAlertCount()
// — but it is still tracked and still reported to the app.
// Persisted (sweep-st/beepmask): headless means a power cycle must not undo it.
static uint8_t beepMask = BEEP_MASK_ALL;

// Write every sounded alert to flash (alert_log.h). Off by default and
// persisted (sweep-st/log) -- headless means a power cycle must not undo it.
static bool alertLogOn = false;

// Returns true if the alert was recorded (i.e. it will actually sound).
static bool noteAlert(int weight, const char* category) {
    if (weight < CONF_ALERT_MIN) return false;
    // Hunting: the clicker and the hunt meter are the only sound and light. A
    // category jingle would stomp the clicker and its animation would cover the
    // meter, on the one walk where you are listening to a single device. Not
    // recorded, so the target's alerted flag stays clear and it sounds once the
    // hunt ends if it is still around.
    if (huntMac.length() > 0) return false;
    // Easter egg: the bar is a flashlight or a party trick and nobody holding
    // it is looking for anything. Same contract as hunting -- not recorded, so
    // nothing is logged that you did not hear, the alerted flag stays clear,
    // and a device still around when the egg closes sounds then.
    if (getEggScene() != EGG_OFF) return false;
    AlertCategory cat = alertCategoryFromName(category);
    if (!(beepMask & (1 << cat))) return false;
    if (weight > pendingAlertConf) {
        pendingAlertConf = weight;
        pendingAlertCat = cat;
    }
    return true;
}

// Sound for a target the first time it proves itself, whether or not we had
// already started tracking it.
//
// This used to live only on the "new target" path, which silently lost real
// detections: a BLE device usually splits its data across the advertisement and
// the scan response, and the name — the strongest signal most rules have — often
// arrives only in the second one. The target was therefore created unmatched by
// the first packet, and the packet that actually identified it took the
// "already tracking" path, which never alerted. Drive past a camera, watch it
// appear in the list, hear nothing. Same for any device matched by a rule
// pushed after it was first seen.
//
// The flag lives on the target, so a device beeps once per appearance rather
// than once per advert; it clears naturally when the target goes stale and is
// pruned (WATCHERS_STALE_MS), so something you drive past twice beeps twice.
static void noteAlertForTarget(WatcherTargetInfo& t, int bestWeight, const String& category) {
    if (t.alerted) return;
    if (bestWeight < CONF_ALERT_MIN) return;
    // Ignored: stay quiet, and leave alerted clear so un-ignoring a device that
    // is still here sounds it (the beep-mask contract). Checked after the
    // weight threshold so a sub-threshold advert never pays the parse.
    if (isIgnoredLocked(t.mac)) return;
    // Only burn the once-per-appearance flag if the alert really sounded. A
    // muted category must stay un-flagged, or un-muting it mid-appearance would
    // be silent until the target went stale — the AirTag in your hand would
    // never beep.
    if (noteAlert(bestWeight, category.c_str())) {
        t.alerted = true;
        // The log follows the buzzer exactly: noteAlert() has already said no
        // for a muted category and during a hunt, so what reaches flash is what
        // you actually heard. Only the flag is set here -- this runs on both
        // radio callbacks, and a LittleFS write is tens of milliseconds. The
        // 1 Hz task does the I/O.
        if (alertLogOn) t.logPending = true;
    }
}

// Exact service-UUID match. A 4-hex-digit rule is a 16-bit SIG UUID: it
// matches that 16-bit UUID, or its 128-bit expansion on the Bluetooth base
// (0000xxxx-0000-1000-8000-00805f9b34fb), and nothing else. A longer rule must
// equal the whole UUID. Never a substring: that is how "3100" once matched
// every UUID with those digits in it and beeped (v5, the Raven rules).
#include "detector/ble.inc"

/**
 * @brief Periodic Task to push targets JSON once per second over BLE Serial & WebSerial
 */

static TaskHandle_t watchersWifiHopTaskHandle = NULL;

static void watchersWifiChannelHopperTask(void *pvParameters) {
    (void)pvParameters;
#if CONFIG_IDF_TARGET_ESP32C5
    // Dual-band: the measured schedule for the selected band (c5_radio.h),
    // rebuilt whenever the band setting changes.
    uint8_t channels[20];
    size_t chCount = 0;
    uint8_t hopBand = 0xFF;
    // Per detector run, not per boot: a detector restart (e.g. after a Site
    // Survey capture) warns again if 5 GHz is still rejected.
    bool warned5GhzRejected = false;
#else
    // Hop all US 2.4 GHz channels so a Flock node beaconing off 1/6/11 isn't
    // missed. (ESP32-S3 is 2.4 GHz only.)
    const uint8_t channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
#endif
    int chIndex = 0;
    bool extended = false;
    while (watchersRunning) {
#if CONFIG_IDF_TARGET_ESP32C5
        vTaskDelay(pdMS_TO_TICKS(C5_HOP_DWELL_MS));
        if (hopBand != sweepBand) {
            hopBand = sweepBand;
            chCount = c5BuildHop(hopBand, channels, sizeof(channels));
            chIndex = 0;
        }
#else
        vTaskDelay(pdMS_TO_TICKS(150));
#endif
        if (!watchersRunning) break;
        // A drone beacons on one fixed channel, so a channel that just carried
        // Remote ID earns one extra dwell before moving on -- never two in a
        // row, or a drone overhead would park the hopper and blind the rest.
        bool heard = odidHeard;
        odidHeard = false;
        if (heard && !extended && huntMac.length() == 0) { extended = true; continue; }
        extended = false;
        // Parked while hunting a Wi-Fi device. Sweeping thirteen channels means
        // hearing a given access point about one dwell in thirteen, which is
        // fine for finding things and useless for walking one down: the clicker
        // would go quiet every time the hopper moved on. Detection of everything
        // else pauses for the duration, which is the deal you accept when you
        // lock onto one target.
        int parked = huntChannel;
#if CONFIG_IDF_TARGET_ESP32C5
        if (huntMac.length() > 0 && parked >= 1 && parked <= 177) {   // 5 GHz targets park too
            esp_err_t chErr = esp_wifi_set_channel(parked, WIFI_SECOND_CHAN_NONE);
            if (chErr != ESP_OK && parked > 14 && !warned5GhzRejected) {
                warned5GhzRejected = true;
                // ESP_LOGW is compiled out at CORE_DEBUG_LEVEL=0 (see the
                // alert print above), so this is a plain print -- otherwise a
                // rejected 5 GHz channel is silent on a headless board.
                if (Serial) Serial.printf("[C5] esp_wifi_set_channel rejected 5 GHz channel %d: %s -- band mode may not have applied\n",
                          parked, esp_err_to_name(chErr));
            }
        } else {
            uint8_t ch = channels[chIndex];
            esp_err_t chErr = esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            if (chErr != ESP_OK && ch > 14 && !warned5GhzRejected) {
                warned5GhzRejected = true;
                // ESP_LOGW is compiled out at CORE_DEBUG_LEVEL=0 (see the
                // alert print above), so this is a plain print -- otherwise a
                // rejected 5 GHz channel is silent on a headless board.
                if (Serial) Serial.printf("[C5] esp_wifi_set_channel rejected 5 GHz channel %d: %s -- band mode may not have applied\n",
                          ch, esp_err_to_name(chErr));
            }
            chIndex = (chIndex + 1) % (int)chCount;
        }
#else
        if (huntMac.length() > 0 && parked >= 1 && parked <= 14) {
            esp_wifi_set_channel(parked, WIFI_SECOND_CHAN_NONE);
        } else {
            esp_wifi_set_channel(channels[chIndex], WIFI_SECOND_CHAN_NONE);
            chIndex = (chIndex + 1) % (int)(sizeof(channels) / sizeof(channels[0]));
        }
#endif
    }
    watchersWifiHopTaskHandle = NULL;
    vTaskDelete(NULL);
}

// Insert/update a target known only from a Remote ID decode. Used by the Wi-Fi
// NAN action-frame path, which carries no SSID, no vendor IE and no OUI worth
// scoring — the decode itself is the whole identification.
#include "detector/wifi.inc"

// Operator state lives here so the board comes back exactly as it was left.
// A detector wired into a car loses power every time the engine stops; one on a
// battery pack loses it whenever the pack is swapped. Neither is a reason to
// forget what it was told to do.
#define STATE_NVS_NS "sweep-st"

static void persistState() {
    Preferences prefs;
    if (!prefs.begin(STATE_NVS_NS, false)) {
        ESP_LOGW(TAG, "Could not open %s — state will not survive a reboot", STATE_NVS_NS);
        return;
    }
    if (huntMac.length() > 0) prefs.putString("hunt", huntMac);
    else                      prefs.remove("hunt");
    prefs.putBool("scanall", scanAll);
    prefs.putBool("attack", attackDetect);
    prefs.putUChar("beepmask", beepMask);
    prefs.putBool("log", alertLogOn);
#if CONFIG_IDF_TARGET_ESP32C5
    prefs.putUChar("band", sweepBand);
#endif
    prefs.end();
}

void restoreWatchersState() {
    Preferences prefs;
    if (!prefs.begin(STATE_NVS_NS, true)) return;
    String mac = prefs.getString("hunt", "");
    bool all = prefs.getBool("scanall", false);
    bool atk = prefs.getBool("attack", false);
    uint8_t mask = prefs.getUChar("beepmask", BEEP_MASK_ALL);
    bool logOn = prefs.getBool("log", false);
#if CONFIG_IDF_TARGET_ESP32C5
    uint8_t band = prefs.getUChar("band", BAND_BOTH);
    sweepBand = (band <= BAND_5) ? band : BAND_BOTH;
#endif
    // A blob whose length is not a whole number of entries is from some other
    // format: drop it rather than read garbage MACs into the gate. Read into a
    // local buffer first -- this runs on every startWatchersWatch() (including
    // after a capture), and writing ignoreList/ignoreCount directly here would
    // race a concurrent addIgnore()/removeIgnore() on the NimBLE host task.
    IgnoreEntry igBuf[IGNORE_MAX];
    uint8_t igCount = 0;
    size_t igLen = prefs.getBytesLength("ignore");
    if (igLen > 0 && igLen % sizeof(IgnoreEntry) == 0 && igLen <= sizeof(igBuf)) {
        prefs.getBytes("ignore", igBuf, igLen);
        igCount = (uint8_t)(igLen / sizeof(IgnoreEntry));
    }
    prefs.end();

    beepMask = mask & BEEP_MASK_ALL;
    alertLogOn = logOn;

    scanAll = all;
    attackDetect = atk;
    huntMac = mac;
    huntMac.toUpperCase();
    if (huntMac.length() > 0) {
        // Armed, but silent until the target is actually heard — see the
        // recency check in the 1 Hz task. Clicking on boot for something that
        // may be miles away would be a lie.
        ESP_LOGI(TAG, "Restored hunt target %s", huntMac.c_str());
    }
    if (scanAll) ESP_LOGI(TAG, "Restored report filter: OFF (reporting everything)");
    if (attackDetect) ESP_LOGI(TAG, "Restored attack-gear detection ON");
    if (beepMask != BEEP_MASK_ALL) ESP_LOGI(TAG, "Restored beep mask 0x%02X", beepMask);
    if (alertLogOn) ESP_LOGI(TAG, "Restored alert logging ON (%u records held)",
                             (unsigned)alertLogCount());
    // Copy the restored ignore list in under the mutex. If it can't be taken,
    // skip the copy rather than write ignoreList/ignoreCount unlocked -- memory
    // is already current in that case, since restore only matters at first
    // start (see the create-before-restore order in startWatchersWatch()).
    if (igCount > 0 && watchersMutex != NULL &&
        xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(ignoreList, igBuf, igCount * sizeof(IgnoreEntry));
        ignoreCount = igCount;
        xSemaphoreGive(watchersMutex);
    }
    if (ignoreCount) ESP_LOGI(TAG, "Restored %u ignored device(s)", (unsigned)ignoreCount);
}

// Writes a snapshot taken under watchersMutex by the caller -- persistIgnore()
// itself runs after the lock is released (USB commands in loop() and BLE
// commands on the NimBLE host task can otherwise overlap this NVS write).
static void persistIgnore(const IgnoreEntry* list, uint8_t count) {
    Preferences prefs;
    if (!prefs.begin(STATE_NVS_NS, false)) return;
    if (count) prefs.putBytes("ignore", list, count * sizeof(IgnoreEntry));
    else       prefs.remove("ignore");
    prefs.end();
}

int addIgnore(const String& mac) {
    uint8_t m[6];
    if (!alertLogParseMac(mac.c_str(), m)) return -2;
    if (watchersMutex == NULL) return -3;
    int rc = 0;
    IgnoreEntry snap[IGNORE_MAX];
    uint8_t snapCount = 0;
    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(100)) != pdTRUE) return -3;
    if (isIgnoredBytesLocked(m)) {
        rc = 1;
    } else if (ignoreCount >= IGNORE_MAX) {
        rc = -1;   // refused, never evicted: a device you chose to silence must not start beeping
    } else {
        uint8_t cat = ALERT_GENERIC;
        for (const auto& t : trackedTargets)
            if (t.mac.equalsIgnoreCase(mac)) { cat = (uint8_t)alertCategoryFromName(t.type.c_str()); break; }
        memcpy(ignoreList[ignoreCount].mac, m, 6);
        ignoreList[ignoreCount].cat = cat;
        ignoreCount++;
    }
    if (rc == 0) {
        memcpy(snap, ignoreList, ignoreCount * sizeof(IgnoreEntry));
        snapCount = ignoreCount;
    }
    xSemaphoreGive(watchersMutex);
    if (rc == 0) persistIgnore(snap, snapCount);
    return rc;
}

bool removeIgnore(const String& mac) {
    uint8_t m[6];
    if (!alertLogParseMac(mac.c_str(), m)) return false;
    if (watchersMutex == NULL) return false;
    bool removed = false;
    IgnoreEntry snap[IGNORE_MAX];
    uint8_t snapCount = 0;
    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(100)) != pdTRUE) return false;
    for (uint8_t i = 0; i < ignoreCount; i++) {
        if (memcmp(ignoreList[i].mac, m, 6) != 0) continue;
        ignoreList[i] = ignoreList[--ignoreCount];
        removed = true;
        break;
    }
    if (removed) {
        memcpy(snap, ignoreList, ignoreCount * sizeof(IgnoreEntry));
        snapCount = ignoreCount;
    }
    xSemaphoreGive(watchersMutex);
    if (removed) persistIgnore(snap, snapCount);
    return removed;
}

// Adds the "ignore" array to doc only when the lock was obtained. On a timeout
// the key is left absent -- CMD:CFG must never carry an authoritative empty
// list it didn't actually read (see the buzzerEnabled/hwMutex trap); the app
// treats an absent field as "unknown" and keeps whatever it last painted,
// which the next successful CMD:CFG (or a retry) corrects.
bool getIgnoreJson(JsonDocument& doc) {
    if (watchersMutex == NULL) return false;
    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(50)) != pdTRUE) return false;
    JsonArray out = doc["ignore"].to<JsonArray>();
    for (uint8_t i = 0; i < ignoreCount; i++) {
        char buf[18];
        const uint8_t* b = ignoreList[i].mac;
        snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
        JsonArray e = out.add<JsonArray>();
        e.add(buf);
        e.add(ignoreList[i].cat);
    }
    xSemaphoreGive(watchersMutex);
    return true;
}

void setBeepMask(uint8_t mask) {
    beepMask = mask & BEEP_MASK_ALL;
    persistState();
    ESP_LOGI(TAG, "Beep mask 0x%02X", beepMask);
}

uint8_t getBeepMask() {
    return beepMask;
}

void setAlertLogEnabled(bool enabled) {
    alertLogOn = enabled;
    persistState();
    ESP_LOGI(TAG, "Alert logging %s", enabled ? "ON" : "OFF");
}

bool getAlertLogEnabled() {
    return alertLogOn;
}

#if CONFIG_IDF_TARGET_ESP32C5
void setBand(uint8_t band) {
    // Does not unpark the hopper: a hunt already parked on a channel keeps it
    // until the hunt ends, regardless of the band selected here.
    sweepBand = (band <= BAND_5) ? band : BAND_BOTH;
    persistState();
    ESP_LOGI(TAG, "Wi-Fi band %u (0 both, 1 2.4 GHz, 2 5 GHz)", (unsigned)sweepBand);
}
#endif

void setScanAll(bool enabled) {
    scanAll = enabled;
    persistState();
    ESP_LOGI(TAG, "Report filter %s", enabled ? "OFF (reporting everything)" : "ON (matches only)");
}

bool getScanAll() {
    return scanAll;
}

void setAttackDetect(bool enabled) {
    attackDetect = enabled;
    persistState();
    ESP_LOGI(TAG, "Attack-gear detection %s", enabled ? "ON" : "OFF");
}

bool getAttackDetect() {
    return attackDetect;
}

int getHuntChannel() {
    return huntChannel;
}

uint32_t getAlertCount() {
    return alertsFired;
}

void setHuntTarget(const String& mac) {
    huntMac = mac;
    huntMac.toUpperCase();
    persistState();
    // Silence now; the 1 Hz task starts the clicker within a second if the
    // target is actually being heard. Turning it on here instead would leave
    // huntAudible false while the clicker ran, and the recency check would then
    // see "no change" and never be able to silence it again.
    huntAudible = false;
    huntChannel = 0;
    huntWifiRssiMs = 0;
    pendingAlertConf = 0;   // an alert queued just before the lock must not sound under it
    setGeigerTargetLock(false);
    ESP_LOGI(TAG, "%s", huntMac.length() > 0
             ? ("Hunting " + huntMac).c_str() : "Hunt cleared");
}

String getHuntTarget() {
    return huntMac;
}

void requestRing(const String& mac) {
    pendingRingMac = mac;
    pendingRingMac.toUpperCase();
}

// Make a suspected tracker announce itself, so you can find the thing that is
// following you. Deliberately not a general GATT write primitive: the MAC is
// the only parameter, and the service (Immediate Alert 0x1802), characteristic
// (Alert Level 0x2A06) and value (0x02 = high alert) are fixed here. The old
// build exposed arbitrary service/char/hex writes plus an advertisement
// spoofer; both are gone and should stay gone.
static void performRing(const String& mac) {
    ESP_LOGI(TAG, "Ringing %s", mac.c_str());
    // A GATT connection needs the radio to itself. pauseBle() is the same hook
    // the NUS server uses; the scan restarts in the same call below whatever
    // happens, so a failed connect can never leave the detector deaf.
    pauseBle(true);

    NimBLEClient* client = NimBLEDevice::createClient();
    bool ok = false;
    if (client) {
        // Bound the attempt. This runs ON the 1 Hz task with the scan paused,
        // so every second spent here is a second of no telemetry and a deaf
        // detector. NimBLE's default is 30 s, and a "tracker" that turns out
        // to have no Immediate Alert service — or has wandered out of range —
        // takes the full timeout. Measured on the bench: a ring at the default
        // stalled the push loop long enough to look like a crash.
#if CONFIG_IDF_TARGET_ESP32C5
        client->setConnectTimeout(5000);   // NimBLE 2.x takes milliseconds; 5 would be 5 ms
#else
        client->setConnectTimeout(5);
#endif
        // Connect with the address type we heard. Hardcoding random made every
        // public-address keyfob fail before the write was even attempted.
        bool pub = false;
        if (watchersMutex != NULL && xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            for (auto& t : trackedTargets) if (t.mac.equalsIgnoreCase(mac)) { pub = t.blePublic; break; }
            xSemaphoreGive(watchersMutex);
        }
        NimBLEAddress addr(std::string(mac.c_str()), pub ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM);
        if (client->connect(addr, false)) {
            NimBLERemoteService* svc = client->getService(NimBLEUUID((uint16_t)0x1802));
            if (svc) {
                NimBLERemoteCharacteristic* ch = svc->getCharacteristic(NimBLEUUID((uint16_t)0x2A06));
                if (ch) {
                    uint8_t high = 0x02;
                    ok = ch->writeValue(&high, 1, false);
                }
            }
            client->disconnect();
        }
        NimBLEDevice::deleteClient(client);
    }
    ESP_LOGI(TAG, "Ring %s: %s", mac.c_str(), ok ? "sent" : "failed");

    pauseBle(false);
    // Tell the phone whether the write landed, so its toast is not a guess.
    sendBleSerial("{\"ring\":\"" + mac + "\",\"ok\":" + (ok ? "true" : "false") + "}");
}

static void watchersPeriodicTask(void *pvParameters) {
    (void)pvParameters;
    uint32_t tick = 0;
    while (watchersRunning) {
        // 250 ms tick, full body every fourth. The three ticks in between exist
        // only for the hunt frame: the clicker is driven straight off the scan
        // callback at advert rate, so a phone fed at 1 Hz reads visibly behind
        // the noise you are walking on. ~40 bytes, three times a second, and
        // only while something is actually being hunted.
        vTaskDelay(pdMS_TO_TICKS(250));
        if (!watchersRunning) break;
        if (++tick % 4 != 0) {
            if (huntMac.length() > 0 && huntLastRssiMs != 0 &&
                millis() - huntLastRssiMs <= HUNT_SILENCE_MS) {
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"hunt\":\"%s\",\"hunt_rssi\":%d}",
                         huntMac.c_str(), huntLastRssi);
                sendBleSerial(buf);   // returns early when nobody is subscribed
                // Same mirror as the 1 Hz push: without it a cable host only
                // saw hunt RSSI once a second. ~40 B, 3/s, only while hunting.
                sendUsbLine(buf);
            }
            continue;
        }

        // Breadcrumb for the crash report: plain word writes, no lock. The
        // target count is read without watchersMutex: size() is (end-begin)/
        // sizeof(T), which can tear if a radio callback reallocates the vector
        // mid-read. Harmless for a breadcrumb (crashReportInit() range-checks
        // it); not worth a lock on the 1 Hz path. Gap: this task is deleted
        // during a capture, so the breadcrumb freezes at the capture start.
        crashBreadcrumbTick((uint32_t)trackedTargets.size());

        // Sound anything the radios flagged since the last tick. Done here, off
        // the detection callbacks, so the Wi-Fi promiscuous handler stays fast
        // and a dense area gets one alert per second rather than a continuous
        // tone. triggerAlarm() itself also refuses to retrigger while an alarm
        // is still sounding.
        int alert = pendingAlertConf;
        AlertCategory alertCat = pendingAlertCat;
        pendingAlertConf = 0;
        // The buzzer pattern is the identification: each category is a distinct
        // "word" you learn by ear. Confidence already gated the alert in
        // noteAlert(); here we just sound whichever category won the interval.
        if (alert >= CONF_ALERT_MIN) {
            triggerCategoryAlert(alertCat);
            alertsFired++;
            // The buzzer is the entire interface when nothing is connected, and
            // "did it actually beep?" is otherwise unanswerable without
            // standing next to it. ESP_LOGI is compiled out at
            // CORE_DEBUG_LEVEL=0, so this is a plain print, and it costs
            // nothing when no USB host is attached.
            // Through the USB line lock, like the push: a bare printf here
            // could land inside a bulk SDF:/LOG: line from loop().
            char alertLine[48];
            snprintf(alertLine, sizeof(alertLine), "[ALERT] category=%d weight=%d", (int)alertCat, alert);
            sendUsbLine(alertLine);
        }

        // Ring runs here, on a real task with a real stack, never on the BLE
        // write callback that asked for it.
        if (pendingRingMac.length() > 0) {
            String target = pendingRingMac;
            pendingRingMac = "";
            performRing(target);
        }

        // Keep the log's ordering key moving whether or not anything alerted:
        // it accumulates deltas, so it must be ticked more often than millis()
        // rolls over (49.7 days) on a board wired to a car battery.
        alertLogTick();
        sdLogTick();

        // Alerts waiting to go to flash. Collected under the mutex but WRITTEN
        // outside it: a LittleFS write is tens of milliseconds and the BLE scan
        // callback takes this same mutex, so holding it across the I/O would
        // stall detection for exactly as long as the flash took.
        struct PendingLog { uint8_t mac[6]; uint8_t cat; int8_t rssi; String rule;
                            String name; String ssid; uint8_t ch; uint8_t role;
                            int32_t company; int confidence; };
        std::vector<PendingLog> toLog;

        // Prune stale targets. This mode used to be the only one that never
        // expired anything, so trackedTargets grew for the whole session.
        if (watchersMutex != NULL && xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
            uint32_t now = millis();

            // Before the prune, or a target that alerted and then went stale in
            // the same interval would be erased with its record unwritten.
            for (auto& t : trackedTargets) {
                if (!t.logPending) continue;
                t.logPending = false;
                PendingLog p;
                // Sentinel rows ("Tag flood: Tile", "BLE popup spam") have no
                // address: log them under 02:00:00:00:00:00 (locally administered,
                // so no vendor lookup), named by the row -- the title carries a
                // changing count and would burn a names.txt slot per flood.
                bool sentinel = t.mac.indexOf(' ') >= 0;
                if (sentinel) { memset(p.mac, 0, 6); p.mac[0] = 0x02; }
                else if (!alertLogParseMac(t.mac.c_str(), p.mac)) continue;
                p.cat  = (uint8_t)alertCategoryFromName(t.type.c_str());
                p.rssi = (int8_t)t.rssi;
                p.rule = sentinel ? t.mac : t.matchedRule;
                // The card copy's extra detail (sd_log.h). Deliberately no drone
                // or operator coordinates: that is the user's position.
                p.name = t.name;
                p.ssid = t.ssid;
                p.ch   = t.wifiCh;
                p.role = t.wifiRole;
                p.company = t.bleCompany;
                p.confidence = t.confidence;
                toLog.push_back(p);
            }
            for (size_t i = 0; i < trackedTargets.size(); ) {
                if (now - trackedTargets[i].lastSeenMs > WATCHERS_STALE_MS) {
                    trackedTargets.erase(trackedTargets.begin() + i);
                } else {
                    i++;
                }
            }

            // Click only while the target is actually being heard. The hunt
            // itself stays armed across silence and across reboots; what stops
            // is the noise. Otherwise a board restored from NVS — or one whose
            // target has gone out of range — would tick in your pocket at the
            // "very far away" rate for hours, which reads as "still tracking
            // it" when it means nothing of the sort.
            if (huntMac.length() > 0) {
                bool heard = false;
                for (const auto& t : trackedTargets) {
                    if (t.mac.equalsIgnoreCase(huntMac) &&
                        now - t.lastSeenMs <= HUNT_SILENCE_MS) {
                        heard = true;
                        break;
                    }
                }
                // A Wi-Fi target's RSSI arrives via the volatile above rather
                // than straight into the clicker, so apply the freshest sample
                // here. BLE targets already drove it directly from the scan
                // callback, which is a normal task and may take the mutex.
                if (heard && huntWifiRssiMs != 0 &&
                    now - huntWifiRssiMs <= HUNT_SILENCE_MS) {
                    updateGeigerRssi(huntWifiRssi);
                }
                if (heard != huntAudible) {
                    huntAudible = heard;
                    setGeigerTargetLock(heard, -90);
                    ESP_LOGI(TAG, "Hunt %s: %s", huntMac.c_str(),
                             heard ? "target heard, clicking" : "target lost, silent");
                }
            }
            xSemaphoreGive(watchersMutex);
        }

        // Flash, then the optional card, deliberately outside the mutex and off
        // both radio callbacks. Typically zero or one record per second.
        for (const auto& p : toLog) {
            alertLogWrite(p.mac, p.cat, p.rule.c_str(), p.rssi);
            SdLogFields f;
            f.secs = alertLogSecs();
            memcpy(f.mac, p.mac, 6);
            f.cat = p.cat; f.rssi = p.rssi; f.rule = p.rule;
            f.name = p.name; f.ssid = p.ssid; f.ch = p.ch; f.role = p.role;
            f.company = p.company; f.confidence = p.confidence;
            sdLogWrite(f);
        }

        String jsonStr = getWatchersTargetsJson();
        sendBleSerial(jsonStr);
        sendUsbLine(jsonStr);   // skips the USB mirror when no host is attached
    }
    watchersTaskHandle = NULL;
    vTaskDelete(NULL);
}

void loadWatchersSignatures() {
    if (watchersMutex == NULL) {
        watchersMutex = xSemaphoreCreateMutex();
    }

    ensureSignaturesFileExists();

    File file = LittleFS.open(SIG_FILE_PATH, "r");
    if (!file) {
        ESP_LOGE(TAG, "Could not open %s for loading signatures", SIG_FILE_PATH);
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        ESP_LOGE(TAG, "Failed to parse %s: %s", SIG_FILE_PATH, error.c_str());
        return;
    }

    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        loadedSignatures.clear();
        JsonArray sigs;
        if (doc["signatures"].is<JsonArray>()) {
            sigs = doc["signatures"].as<JsonArray>();
        } else if (doc.is<JsonArray>()) {
            sigs = doc.as<JsonArray>();
        }

        int rejected = 0;
        for (JsonObject s : sigs) {
            WatcherSignature sig;
            sig.name = s["name"] | "";
            sig.category = s["category"] | "";
            sig.oui = s["oui"] | "";
            sig.mfgId = s["mfg_id"] | "";
            sig.deviceName = s["device_name"] | "";
            sig.serviceUuid = s["service_uuid"] | "";
            sig.weight = s["weight"] | 0;
            if (sig.weight < 0 || sig.weight > 100) sig.weight = 0;   // pushed junk = default
            sig.ssidPrefix = s["ssid_prefix"] | "";

            // A prefix with the locally-administered bit (0x02) set is not a
            // vendor OUI at all — it is the signature of a *randomized* MAC, so
            // it matches random phones. Reject it however it got here, including
            // via a rule set pushed from the app.
            if (sig.oui.length() >= 2) {
                char h[3] = { sig.oui[0], sig.oui[1], 0 };
                long firstOctet = strtol(h, NULL, 16);
                if (firstOctet & 0x02) {
                    ESP_LOGW(TAG, "Rejecting locally-administered OUI rule '%s' (%s)",
                             sig.name.c_str(), sig.oui.c_str());
                    rejected++;
                    continue;
                }
            }
            loadedSignatures.push_back(sig);
        }
        if (rejected > 0) ESP_LOGW(TAG, "Rejected %d locally-administered OUI rule(s).", rejected);
        xSemaphoreGive(watchersMutex);
        ESP_LOGI(TAG, "Loaded %d Watcher signatures into memory.", (int)loadedSignatures.size());
    }
}

void startWatchersWatch() {
    if (watchersMutex == NULL) {
        watchersMutex = xSemaphoreCreateMutex();
    }

    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (watchersRunning) {
            xSemaphoreGive(watchersMutex);
            ESP_LOGW(TAG, "Watcher's Watch mode already active.");
            return;
        }
        watchersRunning = true;
        trackedTargets.clear();
        xSemaphoreGive(watchersMutex);
    }

    loadWatchersSignatures();
    restoreWatchersState();

    ESP_LOGI(TAG, "Starting NimBLE scanner for Watcher's Watch...");

    NimBLEScan* pScan = NimBLEDevice::getScan();
#if CONFIG_IDF_TARGET_ESP32C5
    pScan->setScanCallbacks(&watchersScanCallbacks, true);
#else
    pScan->setAdvertisedDeviceCallbacks(&watchersScanCallbacks, true);
#endif
    pScan->setActiveScan(true);
#if CONFIG_IDF_TARGET_ESP32C5
    // Bench-measured on the C5 (experiment rounds 3-4): 25 ms slices every 50 ms
    // hear a 2 s AirTag as evenly as the S3 does at 50/100; 40/100 left 3x the
    // silent stretches, 60%+ duty starved Wi-Fi.
    pScan->setInterval(50);
    pScan->setWindow(25);
#else
    pScan->setInterval(100);
    pScan->setWindow(50);
#endif
    // BOTH boards: NimBLE keeps every scan result by default (0xFF) and only
    // clears them when a scan *completes*, which this one never does — it runs
    // continuously. Every rotating random MAC was an entry that was never freed.
    // Soaked: C5 -4.6 KB/h over 5 h, S3 -6.25 KB/h over 17.6 h (187.6 -> 78.1 KB)
    // with the tracked-target count flat at 41-44, so it is not target churn.
    // 0 = callbacks only; a scannable device is still held for its scan response,
    // so name matching is unaffected. 1.4 and 2.x behave the same here.
    pScan->setMaxResults(0);

    
#if CONFIG_IDF_TARGET_ESP32C5
    pScan->start(0, false, true);
#else
    pScan->start(0, nullptr, false);
#endif
    
    // Start WiFi Promiscuous
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
#if CONFIG_IDF_TARGET_ESP32C5
    // A failed enable here silently degrades the board to 2.4 GHz-only while it
    // keeps reporting band 0 -- and it's headless, so this is the only witness.
    esp_err_t ccErr = esp_wifi_set_country_code(SWEEP_COUNTRY, true);   // gates the legal 5 GHz channels
    if (ccErr != ESP_OK) {
        // ESP_LOGW is compiled out at CORE_DEBUG_LEVEL=0 (see the alert
        // print elsewhere in this file), so this is a plain print -- a
        // headless board's only witness to a failed 5 GHz enable.
        if (Serial) Serial.printf("[C5] esp_wifi_set_country_code failed: %s\n", esp_err_to_name(ccErr));
    }
    esp_err_t bmErr = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);      // let set_channel cross bands
    if (bmErr != ESP_OK) {
        if (Serial) Serial.printf("[C5] esp_wifi_set_band_mode failed: %s\n", esp_err_to_name(bmErr));
    }
#endif
    // Filter in hardware. The callback only ever handles WIFI_PKT_MGMT, so
    // without this every data/ctrl frame in the air reaches the ISR just to be
    // dropped by the software check.
    wifi_promiscuous_filter_t wfilter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&wfilter);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&watchersWifiPromiscuousCallback);
    
    if (watchersWifiHopTaskHandle == NULL) {
        xTaskCreatePinnedToCore(
            watchersWifiChannelHopperTask,
            "WatchersWifiHop",
            4096,
            NULL,
            1,
            &watchersWifiHopTaskHandle,
            SWEEP_TASK_CORE
        );
    }

    ESP_LOGI(TAG, "Watcher's Watch continuous BLE scan started successfully.");

    // Start periodic JSON broadcast task
    if (watchersTaskHandle == NULL) {
        xTaskCreatePinnedToCore(
            watchersPeriodicTask,
            "WatchersPushTask",
            4096,
            NULL,
            1,
            &watchersTaskHandle,
            SWEEP_TASK_CORE
        );
    }
}

void stopWatchersWatch() {
    if (watchersMutex != NULL && xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (!watchersRunning) {
            xSemaphoreGive(watchersMutex);
            return;
        }
        watchersRunning = false;
        xSemaphoreGive(watchersMutex);
    }

    ESP_LOGI(TAG, "Stopping Watcher's Watch BLE scan...");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan) {
        pScan->stop();
        pScan->clearResults();
    }

    // This mode used to leave promiscuous mode and the rx callback running
    // after a mode switch; only watchersRunning=false kept the callback quiet.
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);

    ESP_LOGI(TAG, "Watcher's Watch mode stopped.");
}

bool isWatchersWatchActive() {
    bool active = false;
    if (watchersMutex != NULL && xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        active = watchersRunning;
        xSemaphoreGive(watchersMutex);
    }
    return active;
}

bool watchersTasksStopped() {
    return !watchersRunning && watchersWifiHopTaskHandle == NULL && watchersTaskHandle == NULL;
}

String getWatchersTargetsJson() {
    JsonDocument doc;
    doc["status"] = "success";
    doc["mode"] = 2;
    doc["name"] = "MODE_DETECTOR";
    // Carried on every push so flash.py --auto can read the tier back over
    // serial (the old selector heartbeat that used to carry it is gone).
    doc["tier"] = getTier();
    // So the app never implies "match" for a row that is only being listed
    // because the filter is off.
    if (scanAll) doc["scan_all"] = true;
    // Absent = off, like scan_all; the app adopts it. Persisted, so a board
    // that ran headless comes back with attack detection however it was left.
    if (attackDetect) doc["attack"] = true;
    // Absent = off, same as attack. Unlike beep_mask/buzzer there is no
    // ambiguity to protect against: "firmware too old to send it" and "the
    // default" both mean nothing is being logged, so this costs 0 B when off.
    if (alertLogOn) doc["log"] = true;
    if (huntMac.length() > 0) doc["hunt"] = huntMac;
    // Echoed on EVERY push, not just in the CMD:CFG reply, so the app's sound
    // controls reconcile continuously instead of once per connection. They used
    // to live only in that reply, which the app asks for three times and then
    // never again -- so a single dropped write or dropped notification left the
    // phone showing a mask the device did not have, for the rest of the session,
    // with no way back but a factory reset. ~30 B on an ~84 B idle payload;
    // telemetry is the tightest budget on the device, so nothing else joins
    // these three without re-measuring the 60 s push soak. (The LED mode
    // joined for the same reason as the other two, ~8 B. The alert count joined
    // so the header's "alerts since power-on" ticks live, ~12 B. The theme
    // joined like the LED mode, ~10 B.)
    //
    // Unconditional, unlike scan_all: "absent" must not be ambiguous between
    // "the default" and "firmware too old to send it".
    doc["beep_mask"] = beepMask;
    doc["buzzer"] = isBuzzerEnabled();
    doc["led"] = getLedMode();
    doc["theme"] = getTheme();
    doc["vol"] = getVolume();
#if CONFIG_IDF_TARGET_ESP32C5
    doc["band"] = getBand();   // C5 only, ~9 B; the device is the authority
#endif
    doc["alerts"] = getAlertCount();

    if (watchersMutex != NULL && xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(200)) == pdTRUE) {

        // Report only signature matches. The phone used to classify the full
        // harvest geospatially (a radio pinned to one place across visits =
        // infrastructure), so the device shipped every MAC it heard. That
        // classifier — and its location history — is gone for opsec, so the
        // live scope now shows exactly what tripped the detector: confirmed
        // matches, nothing else. Round-robin by staleness still fairly rotates
        // when matches exceed the per-push cap (rare).
        // scanAll lifts the listing gate only (see setScanAll). Everything is
        // tracked either way; this decides what gets sent.
        std::vector<size_t> order;
        for (size_t i = 0; i < trackedTargets.size(); i++) {
            if (scanAll || trackedTargets[i].confidence >= CONF_LIST_MIN) order.push_back(i);
        }
        std::sort(order.begin(), order.end(), [](size_t a, size_t b) {
            return trackedTargets[a].lastReportedMs < trackedTargets[b].lastReportedMs;
        });
        doc["count"] = order.size();
        // The cap is lower while the filter is off. Selection stays round-robin
        // by staleness, never by RSSI, so nothing is starved -- every device
        // still comes round, just over a few seconds instead of one. That trade
        // is worth it: an oversized push is not slower, it is *lost*, because
        // one dropped BLE notification discards the entire batch.
        size_t cap = scanAll ? WATCHERS_MAX_REPORT_ALL : WATCHERS_MAX_REPORT;
        if (order.size() > cap) order.resize(cap);

        // The hunted target rides in every push, whatever the round-robin says.
        // Otherwise a crowded room (cap 18 with the filter off) reports it every
        // few seconds and the phone's meter goes stale between appearances --
        // the one row where that is unacceptable. It takes the front slot rather
        // than an extra one, so the payload cap is untouched. Also covers a
        // target below CONF_LIST_MIN that would not otherwise be listed at all.
        if (huntMac.length() > 0) {
            size_t hunted = trackedTargets.size();
            for (size_t i = 0; i < trackedTargets.size(); i++) {
                if (trackedTargets[i].mac.equalsIgnoreCase(huntMac)) { hunted = i; break; }
            }
            if (hunted < trackedTargets.size()) {
                auto at = std::find(order.begin(), order.end(), hunted);
                if (at != order.end()) order.erase(at);
                else if (order.size() >= cap) order.pop_back();
                order.insert(order.begin(), hunted);
            }
        }

        uint32_t nowMs = millis();
        JsonArray targetsArr = doc["targets"].to<JsonArray>();
        for (size_t idx : order) {
            WatcherTargetInfo& t = trackedTargets[idx];
            t.lastReportedMs = nowMs;
            JsonObject obj = targetsArr.add<JsonObject>();
            obj["mac"] = t.mac;
            // Most harvested devices match no rule, so skip the empty strings —
            // at 40 targets/sec those bytes are pure BLE airtime.
            if (t.name.length() > 0)        obj["name"] = t.name;
            if (t.type.length() > 0)        obj["type"] = t.type;
            if (t.matchedRule.length() > 0) obj["matched_rule"] = t.matchedRule;
            obj["rssi"] = t.rssi;
            // first_seen_ms / last_seen_ms deliberately not sent: the app
            // tracks its own wall-clock timing and ignored these, and at ~40
            // targets they were about a quarter of the payload.
            obj["protocol"] = t.protocol.length() > 0 ? t.protocol : "BLE";
            // Worth its bytes even in a filter-off push: a network name is the
            // one field that lets a person recognise their own hardware.
            if (t.ssid.length() > 0) obj["ssid"] = t.ssid;
            // Vendor and link-role hints, a few bytes each and only when known.
            // The app does the name lookups offline from bundled lists.
            if (t.wifiRole > 0)    obj["ap"]  = t.wifiRole == 2 ? 1 : 0;
            if (t.blePublic)       obj["pub"] = 1;
            if (t.bleCompany >= 0) obj["cid"] = t.bleCompany;
            if (t.bleRing)         obj["ring"] = 1;   // the app offers Ring only here
            if (t.wifiCh > 0)      obj["ch"]  = t.wifiCh;   // ~7-9 B, Wi-Fi rows only

            // A device that matched nothing is only in this list because the
            // filter is off, and the app shows it as an address, a protocol and
            // a signal — it renders no count, no confidence and no tier for it.
            // Sending them anyway tripled the size of the row that dominates a
            // filter-off push.
            //
            // This matters more than the byte count suggests. BLE notifications
            // are unacknowledged and a push is split across many of them, so a
            // single dropped chunk truncates the JSON and the app discards the
            // whole second — which looks exactly like "the device found
            // nothing". Fewer, smaller notifications is the difference between
            // the filter-off view working and appearing empty.
            if (t.confidence > 0) {
                obj["count"] = t.count;
                obj["confidence"] = t.confidence;
                obj["tier"] = t.tier.length() > 0 ? t.tier : "Possible";
            }

            // Decoded Remote ID, drones only. Telemetry is the tightest budget
            // on this board, so each field ships once and only when it holds a
            // real value — ODID's sentinels (0/0 for position, -1000 m, 361 deg)
            // mean "unknown" and are dropped rather than sent. Sky Sweeper used
            // to emit every field twice under two names for app compatibility;
            // one name per field here.
            if (t.hasDrone) {
                if (t.uasId.length() > 0)      obj["uas_id"] = t.uasId;
                if (t.operatorId.length() > 0) obj["operator_id"] = t.operatorId;
                if (t.selfId.length() > 0)     obj["self_id"] = t.selfId;
                if (t.droneLat != 0.0 || t.droneLng != 0.0) {
                    obj["lat"] = t.droneLat;
                    obj["lng"] = t.droneLng;
                }
                if (t.altMsl    > -1000) obj["alt"] = t.altMsl;
                if (t.heightAgl > -1000) obj["agl"] = t.heightAgl;
                if (t.speed     > 0)     obj["speed"] = t.speed;
                if (t.heading   < 361)   obj["heading"] = t.heading;
                if (t.opLat != 0.0 || t.opLng != 0.0) {
                    obj["op_lat"] = t.opLat;
                    obj["op_lng"] = t.opLng;
                }
            }
        }
        xSemaphoreGive(watchersMutex);
    } else {
        doc["status"] = "error";
        doc["message"] = "Mutex acquisition timeout";
    }

    String output;
    serializeJson(doc, output);
    return output;
}

String getWatchersSignaturesJson() {
    ensureSignaturesFileExists();

    File file = LittleFS.open(SIG_FILE_PATH, "r");
    if (!file) {
        return "{\"status\":\"error\",\"message\":\"Failed to open signatures file\"}";
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        return "{\"status\":\"error\",\"message\":\"Corrupt signatures JSON file\"}";
    }

    if (doc.is<JsonObject>()) {
        doc["status"] = "success";
        String output;
        serializeJson(doc, output);
        return output;
    } else if (doc.is<JsonArray>()) {
        JsonDocument resDoc;
        resDoc["status"] = "success";
        resDoc["signatures"] = doc;
        String output;
        serializeJson(resDoc, output);
        return output;
    }

    return "{\"status\":\"error\",\"message\":\"Invalid JSON structure\"}";
}

/**
 * @brief Delete /data/signatures.json and reload, regenerating the built-in
 * defaults. The counterpart to updateWatchersSignaturesJson(): once a rule set
 * has been pushed there is otherwise no way back to the defaults short of a
 * full factory reset (BOOT held 5 s), which also wipes the mode and target lock.
 */
void resetWatchersSignaturesToDefaults() {
    LittleFS.remove(SIG_FILE_PATH);
    loadWatchersSignatures();   // ensureSignaturesFileExists() rewrites defaults
    ESP_LOGI(TAG, "Signature rules reset to built-in defaults.");
}

bool updateWatchersSignaturesJson(const String& jsonContent) {
    if (watchersMutex == NULL) {
        watchersMutex = xSemaphoreCreateMutex();
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, jsonContent);
    if (error) {
        ESP_LOGE(TAG, "Failed to parse signature JSON input: %s", error.c_str());
        return false;
    }

    JsonArray sigs;
    if (doc["signatures"].is<JsonArray>()) {
        sigs = doc["signatures"].as<JsonArray>();
    } else if (doc.is<JsonArray>()) {
        sigs = doc.as<JsonArray>();
    } else {
        ESP_LOGE(TAG, "Invalid signatures format: missing 'signatures' array");
        return false;
    }

    if (!LittleFS.exists("/data")) {
        LittleFS.mkdir("/data");
    }

    File file = LittleFS.open(SIG_FILE_PATH, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open %s for writing", SIG_FILE_PATH);
        return false;
    }

    JsonDocument outDoc;
    // Stamp the current schema version. Without it ensureSignaturesFileExists()
    // — which loadWatchersSignatures() calls below — reads version 0, decides
    // the file predates the current defaults, and deletes it. That silently
    // threw away every pushed rule set and made this command a no-op.
    // A rule set pushed by a client running today's protocol is current by
    // definition; it will still be regenerated if SIG_SCHEMA_VERSION is bumped
    // later, which is the intended trade (stale poisoned defaults are worse).
    outDoc["version"] = SIG_SCHEMA_VERSION;
    JsonArray outSigs = outDoc["signatures"].to<JsonArray>();

    for (JsonObject s : sigs) {
        JsonObject ns = outSigs.add<JsonObject>();
        ns["name"] = s["name"] | "Unnamed Rule";
        ns["category"] = s["category"] | "Surveillance";
        ns["oui"] = s["oui"] | "";
        ns["mfg_id"] = s["mfg_id"] | "";
        ns["device_name"] = s["device_name"] | "";
        ns["service_uuid"] = s["service_uuid"] | "";
        // Both optional; written only when set so the file stays readable.
        if (s["weight"].is<int>() && s["weight"].as<int>() > 0) ns["weight"] = s["weight"].as<int>();
        if (s["ssid_prefix"].is<const char*>() && strlen(s["ssid_prefix"].as<const char*>()) > 0)
            ns["ssid_prefix"] = s["ssid_prefix"].as<const char*>();
    }

    serializeJsonPretty(outDoc, file);
    file.close();

    ESP_LOGI(TAG, "Signatures updated successfully in %s", SIG_FILE_PATH);

    // Reload in memory dynamically
    loadWatchersSignatures();

    return true;
}

#ifdef SIGNALSWEEP_CARDPUTER_ADV
static void copyUiText(char* out, size_t capacity, const String& text) {
    size_t n = 0;
    while (n + 1 < capacity && n < text.length()) {
        uint8_t c = text[n];
        out[n++] = c >= 32 && c < 127 ? c : '?';
    }
    out[n] = 0;
}

bool getScannerRows(ScannerRow* rows, size_t capacity, size_t& count, size_t& total) {
    if (!rows || !capacity || !watchersMutex ||
        xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(10)) != pdTRUE) return false;
    count = total = 0;
    const uint32_t now = millis();
    for (const auto& t : trackedTargets) {
        if ((!scanAll && t.confidence < CONF_LIST_MIN) || now - t.lastSeenMs > WATCHERS_STALE_MS) continue;
        ++total;
        size_t pos = scanner_view::insertionPoint(rows, count, t.rssi, t.mac.c_str());
        if (pos >= capacity) continue;
        if (count < capacity) ++count;
        for (size_t j = count - 1; j > pos; --j) rows[j] = rows[j - 1];
        ScannerRow& row = rows[pos];
        copyUiText(row.mac, sizeof(row.mac), t.mac);
        copyUiText(row.name, sizeof(row.name), t.name.length() ? t.name : t.ssid.length() ? t.ssid : t.mac);
        copyUiText(row.type, sizeof(row.type), t.type);
        copyUiText(row.rule, sizeof(row.rule), t.matchedRule);
        copyUiText(row.protocol, sizeof(row.protocol), t.protocol);
        row.rssi = t.rssi; row.confidence = t.confidence;
        row.channel = t.wifiCh; row.lastSeenMs = t.lastSeenMs;
    }
    xSemaphoreGive(watchersMutex);
    return true;
}
#endif
