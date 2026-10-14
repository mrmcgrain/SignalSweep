// SPDX-License-Identifier: GPL-3.0-or-later
// Shared verbatim by ESP32 radio callbacks and the Android native adapter.
static bool uuidMatches(const NimBLEUUID& u, const String& ruleUuid) {
    String want = ruleUuid;
    want.trim();
    want.toLowerCase();
    if (want.startsWith("0x")) want = want.substring(2);
    if (want.length() == 4) {
        uint16_t v = (uint16_t)strtoul(want.c_str(), NULL, 16);
        if (u.bitSize() == 16) return u == NimBLEUUID(v);
        String full = String(u.toString().c_str());
        full.toLowerCase();
        return full == ("0000" + want + "-0000-1000-8000-00805f9b34fb");
    }
    String have = String(u.toString().c_str());
    have.toLowerCase();
    return have == want;
}

/**
 * @brief Match a device against one signature rule. Returns an accumulated
 * confidence weight (0 = no match). A rule ANDs its non-empty conditions; the
 * returned weight is the sum of the matched conditions' weights (capped 100).
 * A rule's own `weight`, when set, replaces that sum outright.
 */
static int matchDeviceAgainstRule(SWEEP_ADV* dev, const WatcherSignature& sig, String& outMatchedRule, String& outCategory) {
    // SSID rules are Wi-Fi-only; the promiscuous callback scores them.
    if (sig.ssidPrefix.length() > 0) return 0;
    if (ruleGatedOff(sig)) return 0;   // attack gear, Attack off

    int weight = 0;

    // 1. Check OUI (MAC Prefix)
    if (sig.oui.length() > 0) {
        String mac = String(dev->getAddress().toString().c_str());
        String cleanMac = "";
        for (size_t i = 0; i < mac.length(); i++) {
            char c = mac[i];
            if (c != ':' && c != '-') cleanMac += (char)toupper(c);
        }
        String cleanOui = "";
        for (size_t i = 0; i < sig.oui.length(); i++) {
            char c = sig.oui[i];
            if (c != ':' && c != '-') cleanOui += (char)toupper(c);
        }

        if (!cleanMac.startsWith(cleanOui)) {
            return 0;
        }
        weight += W_OUI;
    }

    // 2. Check Manufacturer ID
    if (sig.mfgId.length() > 0) {
        if (!dev->haveManufacturerData()) {
            return 0;
        }
        std::string mfg = dev->getManufacturerData();
        if (mfg.length() < 2) {
            return 0;
        }
        uint16_t devMfgId = static_cast<uint8_t>(mfg[0]) | (static_cast<uint8_t>(mfg[1]) << 8);

        String mfgStr = sig.mfgId;
        mfgStr.trim();
        uint16_t targetMfgId = 0;
        if (mfgStr.startsWith("0x") || mfgStr.startsWith("0X")) {
            targetMfgId = static_cast<uint16_t>(strtoul(mfgStr.c_str(), NULL, 16));
        } else {
            targetMfgId = static_cast<uint16_t>(strtoul(mfgStr.c_str(), NULL, 0));
        }

        if (devMfgId != targetMfgId) {
            return 0;
        }
        weight += W_MFG;
    }

    // 3. Check Device Name Substring
    if (sig.deviceName.length() > 0) {
        if (!dev->haveName()) {
            return 0;
        }
        String devName = String(dev->getName().c_str());
        devName.toLowerCase();
        String targetName = sig.deviceName;
        targetName.toLowerCase();

        if (devName.indexOf(targetName) < 0) {
            return 0;
        }
        weight += W_NAME;
    }

    // 4. Check Service UUID
    if (sig.serviceUuid.length() > 0) {
        if (!dev->haveServiceUUID()) {
            return 0;
        }
        bool uuidMatched = false;
        size_t count = dev->getServiceUUIDCount();
        for (size_t i = 0; i < count; i++) {
            if (uuidMatches(dev->getServiceUUID(i), sig.serviceUuid)) {
                uuidMatched = true;
                break;
            }
        }
        if (!uuidMatched) {
            return 0;
        }
        weight += W_UUID;
    }

    if (weight == 0) {
        return 0;  // rule had no conditions
    }
    // A rule that states its own strength replaces the per-condition sum.
    if (sig.weight > 0) weight = sig.weight;

    outMatchedRule = sig.name.length() > 0 ? sig.name : "Matched Signature";
    outCategory = sig.category.length() > 0 ? sig.category : "Surveillance";
    return weight > 100 ? 100 : weight;
}

// Protocol detectors. The tracker check is presence-only (an AirTag's payload
// is a rotating public key; there is nothing to decode). The drone check is a
// full ASTM F3411 decode via the stock opendroneid reference implementation —
// Remote ID is a broadcast standard whose whole point is to be readable, so a
// drone hands us its serial, its position and the operator's position for free.
//
// ODID_UAS_Data is ~1 KB. Both callbacks run on tight stacks (the NimBLE scan
// task and the Wi-Fi promiscuous callback), so these are file-static rather
// than locals. bleUas is only touched inside the watchersMutex block; wifiUas
// only from the single-threaded promiscuous callback. Do not cross them.
static ODID_UAS_Data bleUas;
static ODID_UAS_Data wifiUas;
// Pwnagotchi IE-222 reassembly buffer. File-static for the same reason as
// wifiUas: touched only from the single-threaded promiscuous callback, and
// too big to want on that tight callback stack.
static char pwngridBuf[513];
// gunzip scratch for a compressed pwngrid advertisement (S3 only). The
// decompressor state is ~11 KB, far too big for the promiscuous callback stack;
// both are file-static and touched only from that single-threaded callback, like
// wifiUas/pwngridBuf.
#ifdef PWN_HAS_GUNZIP
static tinfl_decompressor pwnInflator;
static char pwnJson[1024];
#endif

// True if a decode produced anything worth reporting.
static bool odidUseful(const ODID_UAS_Data& d) {
    return d.BasicIDValid[0] || d.LocationValid || d.SystemValid ||
           d.OperatorIDValid || d.SelfIDValid;
}

// Feed one ODID payload to the decoder, picking packed vs single message.
static void odidDecodeInto(ODID_UAS_Data& out, const uint8_t* data, size_t len) {
    if (len == 0) return;
    if ((data[0] & 0xF0) == (ODID_MESSAGETYPE_PACKED << 4)) {
        if (len < 3) return;
        odid_message_process_pack(&out, (uint8_t*)data, len);
    } else if (len >= ODID_MESSAGE_SIZE) {
        decodeOpenDroneID(&out, (uint8_t*)data);
    }
}

// Copy a decoded frame onto a target. Fields are only overwritten when this
// frame actually carried them — Remote ID arrives as a stream of different
// message types, so a Location-only frame must not blank the serial we learned
// from an earlier Basic ID frame.
static void applyDroneData(WatcherTargetInfo& t, const ODID_UAS_Data& d) {
    t.hasDrone = true;
    if (d.BasicIDValid[0] && d.BasicID[0].UASID[0]) t.uasId = String(d.BasicID[0].UASID);
    if (d.OperatorIDValid && d.OperatorID.OperatorId[0])  t.operatorId = String(d.OperatorID.OperatorId);
    if (d.SelfIDValid && d.SelfID.Desc[0])                t.selfId = String(d.SelfID.Desc);
    if (d.LocationValid) {
        t.droneLat  = d.Location.Latitude;
        t.droneLng  = d.Location.Longitude;
        t.altMsl    = d.Location.AltitudeGeo;
        t.heightAgl = d.Location.Height;
        t.speed     = d.Location.SpeedHorizontal;
        t.heading   = d.Location.Direction;
    }
    if (d.SystemValid) {
        t.opLat = d.System.OperatorLatitude;
        t.opLng = d.System.OperatorLongitude;
    }
}

// Decode ASTM Remote ID out of a BLE advert (UUID 0xFFFA in AD type 0x16,
// Service Data - 16-bit UUID). Returns true and fills `out` on a useful decode.
static bool bleDecodeRemoteId(SWEEP_ADV* dev, ODID_UAS_Data& out) {
#if CONFIG_IDF_TARGET_ESP32C5
    const std::vector<uint8_t>& raw = dev->getPayload();   // NimBLE 2.x: a reference, no copy
    const uint8_t* payload = raw.data();
    size_t len = raw.size();
#else
    uint8_t* payload = dev->getPayload();
    size_t len = dev->getPayloadLength();
#endif
    if (!payload || len < 4) return false;
    size_t offset = 0;
    while (offset + 1 < len) {
        uint8_t adLen = payload[offset];
        if (adLen == 0 || offset + 1 + adLen > len) break;
        uint8_t adType = payload[offset + 1];
        if (adType == 0x16 && adLen >= 3) {
            uint16_t uuid = payload[offset + 2] | (payload[offset + 3] << 8);
            if (uuid == 0xFFFA) {
                // ASTM F3411 BT4 framing: UUID, app code 0x0D, a 1-byte message
                // counter, THEN the 25-byte message (1+2+1+1+25 = adLen 30).
                // This used to skip the app code and decode from the counter,
                // shifting every field by a byte; the bench emitter sent no
                // app code or counter at all, so the bench never showed it.
                memset(&out, 0, sizeof(out));
                if (adLen >= 5 + ODID_MESSAGE_SIZE && payload[offset + 4] == 0x0D) {
                    odidDecodeInto(out, &payload[offset + 6], adLen - 5);
                }
                // A malformed or unsupported frame still means "a drone is
                // broadcasting Remote ID here", which is the alert-worthy fact.
                // Report presence either way; the decoded detail is a bonus.
                return true;
            }
        }
        offset += (adLen + 1);
    }
    return false;
}

// True if a BLE advert is an Apple "Find My" offline-finding beacon (an AirTag
// or other Find My tracker). Apple manufacturer data (company 0x004C) with
// message type 0x12. Deliberately NOT a bare 0x004C match — that is every
// iPhone/AirPod in range.
// The message LENGTH is the discriminator, and it is load-bearing. Apple sends
// type 0x12 in two shapes: a 2-byte status ping (`4c 00 12 02 00 02`) that every
// iPhone, iPad and Mac emits constantly, and the 25-byte offline-finding payload
// that carries the rotating public key — i.e. an item that is findable while
// SEPARATED from its owner. Measured across 18 bench/field captures: 1095
// distinct MACs sent the short form, 64 sent the long one. Matching on the type
// byte alone called all 1155 of them trackers, which is how a rural convenience
// store reported 22 AirTags (they were the customers' phones). A category that
// fires on every phone in the room is worse than no category at all.
//
// Tradeoff, accepted: an AirTag still beside its owner advertises the short form
// and is not flagged — but that is the case where the owner is standing next to
// you anyway. A planted tracker is separated by definition, so it sends 0x19.
static bool bleIsAirtag(SWEEP_ADV* dev) {
    if (!dev->haveManufacturerData()) return false;
    std::string mfg = dev->getManufacturerData();
    if (mfg.length() < 4) return false;
    uint16_t company = static_cast<uint8_t>(mfg[0]) | (static_cast<uint8_t>(mfg[1]) << 8);
    return company == 0x004C && static_cast<uint8_t>(mfg[2]) == 0x12
                             && static_cast<uint8_t>(mfg[3]) == 0x19;
}

// Flock's solar battery pack, codename "Penguin" (Nordic; the camera's own app
// calls it ExternalBattery). A Falcon itself never advertises BLE and keeps its
// Wi-Fi off on LTE, so this battery is the only radio on the pole -- and the
// camera holds a permanent link to it, so a healthy install is usually silent.
// It advertises when that link is down (camera rebooting, dead, or unpaired).
// Source: a published Falcon firmware dump (2026-09-21) for the codename and
// the held link; upstream oui-spy notes for the advert (facts only, no code):
//   before ~March 2025  name "Penguin-" + 10-digit serial
//   after               name is the bare 10-digit serial, mfg 0x09C8 (XUNTONG),
//                       "TN<digits>" in the manufacturer payload
// A bare 10-digit name is NOT enough: Apple Nearby Info adverts carry the
// placeholder name "0102000000" (two in our own captures). Nor is 0x09C8 alone.
static bool isTenDigits(const std::string& s, size_t from) {
    if (s.length() != from + 10) return false;
    for (size_t i = from; i < s.length(); i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

static bool blePenguin(SWEEP_ADV* dev) {
    std::string name = dev->haveName() ? dev->getName() : std::string();
    if (isTenDigits(name, 8) && strncasecmp(name.c_str(), "penguin-", 8) == 0) return true;
    if (!dev->haveManufacturerData()) return false;
    std::string mfg = dev->getManufacturerData();
    if (mfg.length() < 2 || static_cast<uint8_t>(mfg[0]) != 0xC8 || static_cast<uint8_t>(mfg[1]) != 0x09) return false;
    if (isTenDigits(name, 0)) return true;
    for (size_t i = 2; i + 2 < mfg.length(); i++)
        if (mfg[i] == 'T' && mfg[i + 1] == 'N' && mfg[i + 2] >= '0' && mfg[i + 2] <= '9') return true;
    return false;
}

// What an Apple device is DOING, from the Continuity message type — or nullptr
// when this is not Apple manufacturer data. Not a category and not a detection:
// it adds no confidence, so these rows stay out of the filtered list and never
// beep. It exists so that the filter-off view says "Apple: iPhone/iPad/Mac (in
// use)" instead of an anonymous random MAC, which is what made the tracker
// category look like it was firing on everything.
//
// Types are Apple's published-by-observation Continuity set; the ones here are
// every type seen across 18 bench/field captures, counted by distinct MAC:
// 0x10 Nearby Info (685), 0x16 (196), 0x02 iBeacon (126), 0x09 AirPlay (110),
// 0x0C Handoff (45), 0x03 AirPrint (16), 0x13 (11), 0x0F Nearby Action (7).
// Which crowd-sourced finding network this advert belongs to, or "" for none.
// Apple is handled separately (bleIsAirtag) because only Apple shares one
// message type between trackers and ordinary phones. These three do not: a
// phone never advertises them, so the service UUID alone is the detection.
//
//   Tile       0xFEED — the only thing that sends it is a Tile. Verified: 3
//                       distinct Tiles across our own captures.
//   Samsung    0xFD5A registered (enrolled in offline finding, i.e. findable by
//                       any Galaxy nearby) / 0xFD59 unregistered, still in setup.
//   Google FMDN 0xFEAA service data, frame byte 0x40 normal / 0x41 "unwanted
//                       tracking protection" — the tracker itself signalling it
//                       may be following someone. Eddystone shares 0xFEAA with
//                       frame types 0x00/0x10/0x20/0x30, so the frame byte is
//                       what separates a finding beacon from a shop beacon.
//
// Spec sources: Google Find Hub / FMDN accessory spec; Samsung offline-finding
// analysis (arXiv 2210.14702). Checked against our own captures: the Tile arm
// fires on the bench Tile keychain (`d5:95:0b:0a:22:e1`, 61 adverts across six
// captures on three days, up to -40 dBm) plus two Tiles in the field, and one
// real FMDN tag turned up at -89 dBm with frame byte 0x40. **Samsung is the one
// arm no capture has exercised** — it is written from the spec and unproven.
static String bleTrackerNetwork(SWEEP_ADV* dev) {
    if (dev->getServiceDataCount() == 0) return String();

    std::string fmdn = dev->getServiceData(NimBLEUUID((uint16_t)0xFEAA));
    if (fmdn.length() >= 1) {
        uint8_t frame = static_cast<uint8_t>(fmdn[0]);
        if (frame == 0x41) return "Google Find My Tracker (unwanted-tracking mode)";
        if (frame == 0x40) return "Google Find My Tracker";
        return String();   // an ordinary Eddystone beacon, not a tracker
    }
    if (dev->getServiceData(NimBLEUUID((uint16_t)0xFEED)).length() > 0) return "Tile Tracker";
    if (dev->getServiceData(NimBLEUUID((uint16_t)0xFD5A)).length() > 0) return "Samsung SmartTag";
    if (dev->getServiceData(NimBLEUUID((uint16_t)0xFD59)).length() > 0) return "Samsung SmartTag (setup)";
    return String();
}

//
// Nearby Info (0x10) carries one more byte worth decoding: high nibble status
// flags, low nibble an activity code. Field meanings are documented by the
// furiousMAC Continuity project (GPL-2.0 — facts used, no code taken; see
// CREDITS.md). Cross-checked against our own captures, where the packed byte
// lands on 0x31 (reporting disabled, 156 frames), 0x32 (idle, 116), 0x37
// (active, 136) and 0x3B (recent interaction, 149) — i.e. the table still
// describes 2026 devices. Deliberately NOT decoded: lid open/closed. macOS
// simply stops advertising when the lid shuts, which is indistinguishable from
// powered off, asleep or out of range, so reporting it would be a guess.
static String appleRole(SWEEP_ADV* dev) {
    if (!dev->haveManufacturerData()) return String();
    std::string mfg = dev->getManufacturerData();
    if (mfg.length() < 3) return String();
    uint16_t company = static_cast<uint8_t>(mfg[0]) | (static_cast<uint8_t>(mfg[1]) << 8);
    if (company != 0x004C) return String();
    switch (static_cast<uint8_t>(mfg[2])) {
        case 0x10: {
            if (mfg.length() < 5) return "Apple: iPhone/iPad/Mac";
            switch (static_cast<uint8_t>(mfg[4]) & 0x0F) {
                case 0x01: return "Apple: iPhone/iPad/Mac (reporting off)";
                case 0x03: return "Apple: iPhone/iPad/Mac (idle)";
                case 0x05: return "Apple: iPhone/iPad/Mac (audio, screen locked)";
                case 0x07: return "Apple: iPhone/iPad/Mac (active, screen on)";
                case 0x09: return "Apple: iPhone/iPad/Mac (video playing)";
                case 0x0A: return "Apple: Watch (on wrist, unlocked)";
                case 0x0B: return "Apple: iPhone/iPad/Mac (just used)";
                case 0x0D: return "Apple: iPhone (driving)";
                case 0x0E: return "Apple: iPhone (on a call)";
                default:   return "Apple: iPhone/iPad/Mac";
            }
        }
        case 0x12: return "Apple: Find My (with owner)";  // short form; the 0x19 form is the tracker
        case 0x02: return "Apple: iBeacon app";
        case 0x03: return "Apple: AirPrint (printer)";
        case 0x05: return "Apple: AirDrop";
        case 0x06: return "Apple: HomeKit accessory";
        case 0x07: return "Apple: AirPods pairing";       // Proximity Pairing, confirmed 0x07
        case 0x08: return "Apple: Hey Siri";
        case 0x09: return "Apple: AirPlay target (TV/HomePod/Mac)";  // confirmed 0x09
        case 0x0A: return "Apple: AirPlay source";
        case 0x0B: return "Apple: Watch (Magic Switch)";
        case 0x0C: return "Apple: Handoff";
        case 0x0D: return "Apple: tethering target";
        case 0x0E: return "Apple: tethering source (hotspot)";
        case 0x0F: return "Apple: Nearby Action";
        // 0x01, 0x13 and 0x16 are live in our captures (120, 11 and 196 distinct
        // MACs) but are not in any published table, so they stay unnamed rather
        // than guessed at.
        default:   return "Apple device";
    }
}

// Bluetooth SIG company ID from the manufacturer data, or -1 if this advert
// carries none. The app names the vendor from it -- the one vendor hint that
// survives a randomized address, which is most of BLE.
static int32_t bleCompanyId(SWEEP_ADV* dev) {
    if (!dev->haveManufacturerData()) return -1;
    std::string mfg = dev->getManufacturerData();
    if (mfg.length() < 2) return -1;
    return static_cast<uint8_t>(mfg[0]) | (static_cast<uint8_t>(mfg[1]) << 8);
}

// Forward declaration: upsertAttackTarget is defined later in this file (used
// today only from the WiFi promiscuous callback, below its own definition),
// but the BLE-spam detector inside WatchersScanCallbacks::onResult needs it
// too, and that class is defined earlier in the file.
static void upsertAttackTarget(const String& mac, int rssi, const char* proto,
                                const String& title, uint8_t ch = 0,
                                const char* category = HACKING_GEAR_CATEGORY);

// BLE popup-spam state: distinct RANDOM MACs blasting Apple Proximity Pairing
// (0x07) / Nearby Action (0x0F) adverts. BLE scan callback only, no lock. A
// room of real AirPods (public/static addresses) must never trip this -- only
// rapidly-rotating random MACs do, which is why the gate is `!pubAddr`.
static BleSpamState bleSpamState;

// Tag-flood state, one per tracker type. BLE scan callback only, no lock.
static TagFloodState tagFloodState[TAGFLOOD_TYPES];
static const char* const TAGFLOOD_NAMES[TAGFLOOD_TYPES] = { "Apple Find My", "Google Find My", "Samsung SmartTag", "Tile" };
static int tagFloodType(SWEEP_ADV* dev) {
    if (bleIsAirtag(dev)) return 0;
    String n = bleTrackerNetwork(dev);
    if (n.startsWith("Google")) return 1;
    if (n.startsWith("Samsung")) return 2;
    if (n.startsWith("Tile")) return 3;
    return -1;
}

/**
 * @brief NimBLE Scan Callbacks for Watcher's Watch
 */
#if CONFIG_IDF_TARGET_ESP32C5
class WatchersScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
#else
class WatchersScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
#endif
        if (!watchersRunning) return;

        if (watchersMutex == NULL) return;

        String mac = String(advertisedDevice->getAddress().toString().c_str());
        mac.toUpperCase();
        int rssi = advertisedDevice->getRSSI();
        uint32_t now = millis();
        String devName = advertisedDevice->haveName() ? String(advertisedDevice->getName().c_str()) : "";
        int addressType = advertisedDevice->getAddress().getType();
        bool pubAddr = addressType == BLE_ADDR_PUBLIC;
        int32_t company = bleCompanyId(advertisedDevice);
        // ponytail: advertised UUIDs only; a tag that hides 0x1802 in GATT gets no Ring button.
        bool ringable = advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0x1802)) ||
                        advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0x1803));

        // BLE popup spam (Flipper "BLE spam" and friends): floods of Apple
        // Proximity-Pairing / Nearby-Action adverts from rotating random MACs
        // that make nearby iPhones throw pairing popups. Count DISTINCT random
        // MACs sending 0x07/0x0F in the window; public/static Apple gear is
        // real traffic and excluded by !pubAddr. Attack toggle only. Keys on
        // a hash of the MAC STRING (not raw address bytes) -- the distinct-set
        // only needs a stable per-MAC key, same technique karma uses for
        // SSIDs, and it sidesteps any NimBLE 1.4-vs-2.x address-byte-order
        // difference between the S3 and C5 cores. No mutex here (matches
        // deauth/karma); upsertAttackTarget takes watchersMutex itself.
        if (attackDetect && addressType == BLE_ADDR_RANDOM && advertisedDevice->haveManufacturerData()) {
            std::string mfgd = advertisedDevice->getManufacturerData();
            if (mfgd.length() >= 3 &&
                (uint8_t)mfgd[0] == 0x4C && (uint8_t)mfgd[1] == 0x00 &&
                ((uint8_t)mfgd[2] == 0x07 || (uint8_t)mfgd[2] == 0x0F)) {
                uint64_t key = attackHash((const uint8_t*)mac.c_str(), mac.length());
                uint32_t nMacs = bleSpamNote(&bleSpamState, key, rssi, now);
                if (nMacs >= ATTACK_BLESPAM_THRESHOLD) {
                    char title[40];
                    snprintf(title, sizeof(title), "%s \xC2\xB7 %u MACs",
                             ATTACK_TITLE_BLESPAM, (unsigned)nMacs);
                    upsertAttackTarget(String(ATTACK_TITLE_BLESPAM), rssi, "BLE", String(title), 0);
                }
            }
        }

        // Tag flood (always on): a spammer's fake trackers each live for an
        // advert or two. Once a type floods, its targets stay listed but skip
        // the beep and the log (Ignore-style, alerted stays clear so a real tag
        // still here sounds when the hold ends); one Tracker row speaks for them.
        bool floodMuted = false;
        int ft = tagFloodType(advertisedDevice);
        if (ft >= 0) {
            TagFloodState* fs = &tagFloodState[ft];
            tagFloodNote(fs, attackHash((const uint8_t*)mac.c_str(), mac.length()), now);
            if (tagFloodActive(fs, now)) {
                floodMuted = true;
                char title[80];
                snprintf(title, sizeof(title), "%s \xC2\xB7 %s \xC2\xB7 %u fake \xC2\xB7 real tags muted",
                         TAGFLOOD_TITLE, TAGFLOOD_NAMES[ft], (unsigned)fs->total);
                upsertAttackTarget(String(TAGFLOOD_TITLE ": ") + TAGFLOOD_NAMES[ft], rssi, "BLE", String(title), 0, "Tracker");
            }
        }

        // Hunting: every advert from the target refreshes the click rate. Done
        // before the mutex so a busy detector never delays the feedback you are
        // physically walking on.
        if (huntMac.length() > 0 && huntMac.equalsIgnoreCase(mac)) {
            updateGeigerRssi(rssi);
            huntLastRssi = rssi;
            huntLastRssiMs = now;
        }

        if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Accumulate confidence across every rule this device matches, so
            // corroborating signals (e.g. OUI + service UUID) add up. Keep the
            // rule/category from the single strongest match for display.
            int confidence = 0;
            int bestWeight = 0;
            String matchedRule = "";
            String matchedCategory = "";

            for (const auto& sig : loadedSignatures) {
                String r = "", c = "";
                int w = matchDeviceAgainstRule(advertisedDevice, sig, r, c);
                if (w > 0) {
                    confidence += w;
                    if (w > bestWeight) {
                        bestWeight = w;
                        matchedRule = r;
                        matchedCategory = c;
                    }
                }
            }

            // Protocol detectors (not signature rules): presence alone is a
            // strong, unambiguous match, so they set the category directly.
            String trackerNet = bleTrackerNetwork(advertisedDevice);
            bool droneDecoded = false;
            if (bleDecodeRemoteId(advertisedDevice, bleUas)) {
                droneDecoded = odidUseful(bleUas);
                confidence += W_DRONE;
                if (W_DRONE > bestWeight) {
                    bestWeight = W_DRONE;
                    matchedRule = "Remote ID Drone";
                    matchedCategory = "Drone";
                }
            } else if (bleIsAirtag(advertisedDevice)) {
                confidence += W_TRACKER;
                if (W_TRACKER > bestWeight) {
                    bestWeight = W_TRACKER;
                    matchedRule = "Apple Find My Tracker";
                    matchedCategory = "Tracker";
                }
            } else if (trackerNet.length() > 0) {
                confidence += W_TRACKER;
                if (W_TRACKER > bestWeight) {
                    bestWeight = W_TRACKER;
                    matchedRule = trackerNet;
                    matchedCategory = "Tracker";
                }
            } else if (blePenguin(advertisedDevice)) {
                confidence += W_PENGUIN;
                if (W_PENGUIN > bestWeight) {
                    bestWeight = W_PENGUIN;
                    matchedRule = "Flock Battery (Penguin)";
                    matchedCategory = "Flock Safety";
                }
            } else if (matchedRule.length() == 0) {
                // Label only. No confidence, no category, so it cannot beep, is
                // not listed while the filter is on, and counts under Everything
                // alone. A real signature rule always wins the name.
                String role = appleRole(advertisedDevice);
                if (role.length() > 0) matchedRule = role;
            }

            if (confidence > 100) confidence = 100;

            // The confidence score is an annotation, not a gate: the phone
            // classifies on geospatial persistence (a radio pinned to one place
            // across repeat visits is infrastructure), which only works if it
            // sees everything the radio hears — including vendors not on any
            // list. CONF_LIST_MIN now only decides whether the buzzer fires.
            {
                bool found = false;
                for (auto& target : trackedTargets) {
                    if (target.mac.equalsIgnoreCase(mac)) {
                        target.rssi = rssi;
                        target.lastSeenMs = now;
                        target.count++;
                        // Corroboration: this MAC was previously seen over WiFi.
                        int merged = confidence;
                        if (target.protocol == "WiFi" || target.protocol == "BLE+WiFi") {
                            merged = confidence + W_CORROBORATION;
                            target.protocol = "BLE+WiFi";
                        } else {
                            target.protocol = "BLE";
                        }
                        if (merged > 100) merged = 100;
                        if (merged > target.confidence) target.confidence = merged;
                        target.tier = tierForConfidence(target.confidence);
                        if (devName.length() > 0) target.name = devName;
                        target.blePublic = pubAddr;
                        // A scan response often has no manufacturer data; don't
                        // let it erase the company the advert already gave.
                        if (company >= 0) target.bleCompany = company;
                        if (ringable) target.bleRing = true;
                        // Don't let a later non-matching advert wipe a category
                        // an earlier rule match established.
                        if (matchedCategory.length() > 0) {
                            target.type = matchedCategory;
                            target.matchedRule = matchedRule;
                        }
                        if (droneDecoded) applyDroneData(target, bleUas);
                        if (!floodMuted) noteAlertForTarget(target, bestWeight, matchedCategory);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    WatcherTargetInfo newTarget;
                    newTarget.mac = mac;
                    newTarget.name = devName;
                    newTarget.type = matchedCategory;
                    newTarget.matchedRule = matchedRule;
                    newTarget.rssi = rssi;
                    newTarget.firstSeenMs = now;
                    newTarget.lastSeenMs = now;
                    newTarget.count = 1;
                    newTarget.protocol = "BLE";
                    newTarget.blePublic = pubAddr;
                    newTarget.bleCompany = company;
                    newTarget.bleRing = ringable;
                    newTarget.confidence = confidence;
                    newTarget.tier = tierForConfidence(confidence);
                    newTarget.lastReportedMs = 0;
                    if (droneDecoded) applyDroneData(newTarget, bleUas);
                    auto* added = appendTrackedTarget(newTarget);

                    // Log only on a signature hit; everything else is tracked
                    // silently. The buzzer decision is noteAlertForTarget's,
                    // and it tests the strongest single signal, not the sum.
                    if (confidence >= CONF_LIST_MIN) {
                        ESP_LOGI(TAG, "[SURVEILLANCE] MAC: %s, Rule: %s, Cat: %s, Conf: %d (%s), RSSI: %d",
                                 mac.c_str(), matchedRule.c_str(), matchedCategory.c_str(),
                                 confidence, newTarget.tier.c_str(), rssi);
                    }
                    if (added && !floodMuted) noteAlertForTarget(*added, bestWeight, matchedCategory);
                }
            }
            xSemaphoreGive(watchersMutex);
        }
    }
};

static WatchersScanCallbacks watchersScanCallbacks;

