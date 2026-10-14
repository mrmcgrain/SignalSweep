// SPDX-License-Identifier: GPL-3.0-or-later
// Shared verbatim by ESP32 radio callbacks and the Android native adapter.
static void upsertDroneTarget(const String& mac, int rssi, const char* proto,
                              const ODID_UAS_Data& d, uint8_t ch = 0) {
    if (watchersMutex == NULL) return;
    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    uint32_t now = millis();
    bool found = false;
    for (auto& t : trackedTargets) {
        if (t.mac.equalsIgnoreCase(mac)) {
            t.rssi = rssi;
            t.lastSeenMs = now;
            t.count++;
            if (W_DRONE > t.confidence) t.confidence = W_DRONE;
            t.tier = tierForConfidence(t.confidence);
            t.type = "Drone";
            t.matchedRule = "Remote ID Drone";
            if (ch > 0) t.wifiCh = ch;
            applyDroneData(t, d);
            noteAlertForTarget(t, W_DRONE, "Drone");
            found = true;
            break;
        }
    }
    if (!found) {
        WatcherTargetInfo t;
        t.mac = mac;
        t.type = "Drone";
        t.matchedRule = "Remote ID Drone";
        t.rssi = rssi;
        t.firstSeenMs = now;
        t.lastSeenMs = now;
        t.count = 1;
        t.protocol = proto;
        t.confidence = W_DRONE;
        t.tier = tierForConfidence(W_DRONE);
        t.lastReportedMs = 0;
        if (ch > 0) t.wifiCh = ch;
        applyDroneData(t, d);
        auto* added = appendTrackedTarget(t);
        ESP_LOGI(TAG, "[DRONE - %s] MAC: %s, UAS: %s, RSSI: %d",
                 proto, mac.c_str(), t.uasId.c_str(), rssi);
        if (added) noteAlertForTarget(*added, W_DRONE, "Drone");
    }
    xSemaphoreGive(watchersMutex);
}

// Surface a rate-detector hit (deauth/karma/BLE-spam) as an ordinary "Hacking
// gear" row. Same shape as upsertDroneTarget: keyed by mac, takes watchersMutex
// briefly (bail on miss -- never block a radio callback), leaves the once-per-
// appearance beep to noteAlertForTarget. `mac` may be a sentinel string for a
// detector with no single transmitter (BLE popup spam).
static void upsertAttackTarget(const String& mac, int rssi, const char* proto,
                               const String& title, uint8_t ch, const char* category) {
    if (watchersMutex == NULL) return;
    if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    uint32_t now = millis();
    bool found = false;
    for (auto& t : trackedTargets) {
        if (t.mac.equalsIgnoreCase(mac)) {
            t.rssi = rssi; t.lastSeenMs = now; t.count++;
            if (W_ATTACK > t.confidence) t.confidence = W_ATTACK;
            t.tier = tierForConfidence(t.confidence);
            t.type = category;
            t.matchedRule = title;
            if (ch > 0) t.wifiCh = ch;
            noteAlertForTarget(t, W_ATTACK, category);
            found = true; break;
        }
    }
    if (!found) {
        WatcherTargetInfo t;
        t.mac = mac; t.type = category; t.matchedRule = title;
        t.rssi = rssi; t.firstSeenMs = now; t.lastSeenMs = now; t.count = 1;
        t.protocol = proto; t.confidence = W_ATTACK; t.tier = tierForConfidence(W_ATTACK);
        t.lastReportedMs = 0; if (ch > 0) t.wifiCh = ch;
        auto* added = appendTrackedTarget(t);
        if (added) noteAlertForTarget(*added, W_ATTACK, category);
    }
    xSemaphoreGive(watchersMutex);
}

// Deauth-burst state: touched ONLY from the single-threaded Wi-Fi promiscuous
// callback, so it needs no lock (same rule as wifiUas/pwngridBuf).
static DeauthState deauthState;

// Karma/PineAP state: one BSSID answering probes for many SSIDs. Promiscuous
// callback only, no lock.
static KarmaState karmaState;

static void watchersWifiPromiscuousCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (!watchersRunning) return;
    if (type != WIFI_PKT_MGMT) return;
    
    wifi_promiscuous_pkt_t *packet = (wifi_promiscuous_pkt_t *)buf;
#if CONFIG_IDF_TARGET_ESP32C5
    // The C5 driver passes frames that failed reception; the S3's never did.
    // They are random bytes (every type/subtype incl. reserved type 3, thousands of
    // fake transmitters on the bench) and would feed the IE walk and OUI rules junk.
    if (packet->rx_ctrl.rx_state != 0) return;
#endif
    uint8_t *payload = packet->payload;
    int length = packet->rx_ctrl.sig_len;
    int rssi = packet->rx_ctrl.rssi;
    
    if (length < 24) return;
    
    uint8_t fc0 = payload[0];
    uint8_t ftype = (fc0 >> 2) & 0x03;
    uint8_t fsubtype = (fc0 >> 4) & 0x0F;
    
    uint8_t *addr1 = payload + 4;
    uint8_t *addr2 = payload + 10;

    // Remote ID over Wi-Fi NAN: an Action frame addressed to the ASTM NAN
    // cluster 51:6F:9A:01:00:00. The opendroneid helper validates and unpacks
    // the whole frame, including the transmitter MAC (which is inside the
    // service descriptor, not the 802.11 header).
    {
        static const uint8_t nanDest[6] = {0x51, 0x6F, 0x9A, 0x01, 0x00, 0x00};
        if (memcmp(nanDest, addr1, 6) == 0) {
            odidHeard = true;
            memset(&wifiUas, 0, sizeof(wifiUas));
            char nanMacRaw[6] = {0};
            if (odid_wifi_receive_message_pack_nan_action_frame(&wifiUas, nanMacRaw, payload, length) == 0
                && odidUseful(wifiUas)) {
                char nanMacBuf[20];
                snprintf(nanMacBuf, sizeof(nanMacBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
                         (uint8_t)nanMacRaw[0], (uint8_t)nanMacRaw[1], (uint8_t)nanMacRaw[2],
                         (uint8_t)nanMacRaw[3], (uint8_t)nanMacRaw[4], (uint8_t)nanMacRaw[5]);
                upsertDroneTarget(String(nanMacBuf), rssi, "WiFi", wifiUas, packet->rx_ctrl.channel);
            }
            return;
        }
    }

    // Deauthentication (subtype 12) / disassociation (subtype 10) burst: an
    // aircrack/Pineapple signature. These already reach us (mgmt filter) and are
    // dropped by the 4/5/8 test below. Count per transmitter in a sliding window;
    // a broadcast deauth (addr1 = ff:ff:ff:ff:ff:ff) hits every client at once and
    // counts double. Attack toggle only -- roaming APs deauth constantly.
    if (attackDetect && ftype == 0 && (fsubtype == 12 || fsubtype == 10)) {
        static const uint8_t bcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        bool isBroadcast = (memcmp(addr1, bcast, 6) == 0);
        uint32_t frames = deauthNote(&deauthState, attackMac48(addr2), isBroadcast, millis());
        if (frames >= ATTACK_DEAUTH_THRESHOLD) {
            char m[18]; snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
                addr2[0], addr2[1], addr2[2], addr2[3], addr2[4], addr2[5]);
            char title[40]; snprintf(title, sizeof(title), "%s \xC2\xB7 %u frames \xC2\xB7 ch %u",
                ATTACK_TITLE_DEAUTH, (unsigned)frames, (unsigned)packet->rx_ctrl.channel);
            upsertAttackTarget(String(m), rssi, "WiFi", String(title), packet->rx_ctrl.channel);
        }
        return;   // a deauth is never a probe/beacon; done with this frame
    }

    // Probe Request (subtype 4) or Beacon (subtype 8) or Probe Response (subtype 5)
    if (ftype == 0 && (fsubtype == 4 || fsubtype == 5 || fsubtype == 8)) {
        if (watchersMutex == NULL) return;
        
        int wifiConfidence = 0;
        int bestWeight = 0;
        String matchedRule = "";
        String matchedCategory = "";
        bool droneDecoded = false;
        String foundSsid = "";
        // The AP's own channel, from the DS Parameter Set IE (id 3, 1-byte
        // payload) if the frame carries one. 0 = absent (e.g. a probe request,
        // which names no channel of its own). rx_ctrl.channel is the hopper's
        // tuned channel, not the transmitter's -- 2.4 GHz adjacent-channel
        // leakage means an AP on 6 is routinely heard while tuned to 4-8, which
        // made the band/channel chip lie. Prefer dsCh when present.
        int dsCh = 0;
        // Gate for the wildcard-probe signature below. True only when the OUI
        // matched a rule whose category is Flock: a Cradlepoint or Sierra router
        // sending a wildcard probe is not a camera, and scoring it as one would
        // be the bandOf() mistake again — a weak hint naming the wrong vendor is
        // worse than no hint at all.
        bool flockOui = false;
        // Beacons and probe responses come from an AP, probe requests from a client.
        uint8_t role = (fsubtype == 4) ? 1 : 2;

        // 1. Check OUI (weak signal)
        String mac = "";
        char macBuf[20];
        snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X", addr2[0], addr2[1], addr2[2], addr2[3], addr2[4], addr2[5]);
        mac = String(macBuf);

        // Hunt hints. Volatile writes only -- no mutex, no buzzer call: this is
        // the promiscuous callback and it must not block. The 1 Hz task applies
        // the RSSI to the clicker and the hopper reads the channel.
        if (huntMac.length() > 0 && huntMac.equalsIgnoreCase(mac)) {
            huntWifiRssi = rssi;
            huntWifiRssiMs = millis();
            huntLastRssi = rssi;
            huntLastRssiMs = huntWifiRssiMs;
            huntChannel = packet->rx_ctrl.channel;
        }
        String cleanMac = "";
        for (size_t i = 0; i < mac.length(); i++) {
            char c = mac[i];
            if (c != ':' && c != '-') cleanMac += (char)toupper(c);
        }

        if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (const auto& sig : loadedSignatures) {
                if (sig.oui.length() > 0 && sig.ssidPrefix.length() == 0 && !ruleGatedOff(sig)) {
                    String cleanOui = "";
                    for (size_t i = 0; i < sig.oui.length(); i++) {
                        char c = sig.oui[i];
                        if (c != ':' && c != '-') cleanOui += (char)toupper(c);
                    }
                    if (cleanMac.startsWith(cleanOui)) {
                        // Wi-Fi only ever evaluates the OUI here -- a rule that
                        // also states mfg_id/device_name/service_uuid has
                        // conditions this callback cannot check (those are
                        // BLE-only fields), so its own weight can only replace
                        // the sum when the OUI is the rule's whole condition.
                        // Otherwise this is a partial match and stays at the
                        // generic Wi-Fi OUI weight, same as an unweighted rule.
                        bool ouiOnly = sig.mfgId.length() == 0 && sig.deviceName.length() == 0 && sig.serviceUuid.length() == 0;
                        int w = (ouiOnly && sig.weight > 0) ? sig.weight : W_WIFI_OUI;
                        wifiConfidence += w;
                        if (sig.category == "Flock Safety") flockOui = true;
                        if (w > bestWeight) {
                            bestWeight = w;
                            matchedRule = sig.name.length() > 0 ? sig.name : "WiFi OUI Match";
                            matchedCategory = sig.category;
                        }
                        break;
                    }
                }
            }
            xSemaphoreGive(watchersMutex);
        }

        // 2. Strong signals: Lite-On Flock vendor IE (50:6F:9A) and Flock SSIDs.
        // Scanned unconditionally so they add to (not replace) the OUI weight.
        if (length > 24 + 12) {
            int offset = (fsubtype == 4) ? 24 : 36;
            int body_len = length - offset - 4; // -4 for FCS
            const uint8_t *body = payload + offset;
            bool ieHit = false, ssidHit = false;
            // The wildcard-probe signature, scored after the loop. The FCS is
            // already stripped by body_len above, which is why this needs none
            // of upstream's retry-with-(len-4) dance — do not "simplify" that
            // subtraction away.
            bool wildcardSsid = false, liteonSig = false;
            // Pwnagotchi (pwngrid) beacons carry the advertisement chunked at 0xFF
            // across vendor IE 222 (IDWhisperPayload), and a 1-byte IE 223
            // (IDWhisperCompression = 0x01) iff that payload is gzip-compressed.
            // Confirmed from pwngrid v1.10.3 source (2026-09-20), not a guess.
            // Reassemble the 222 chunks in-frame, then parse after the walk. Only
            // while the attack toggle is on and only on beacons. pwngridBuf is
            // file-static (below) — the promiscuous callback is single-threaded.
            bool pwnScan = attackDetect && fsubtype == 8;
            bool pwnMac  = (addr2[0]==0xDE && addr2[1]==0xAD && addr2[2]==0xBE &&
                            addr2[3]==0xEF && addr2[4]==0xDE && addr2[5]==0xAD);
            int  pwnLen  = 0;
            bool pwnGzip = false;   // IE 223 seen == payload is gzip

            int b = 0;
            while (b < body_len - 1) {
                uint8_t id = body[b];
                uint8_t elen = body[b+1];
                if (b + 2 + elen > body_len) break;

                // The Flock stack's exact Lite-On vendor IE: tag 221, length 7,
                // payload 50:6f:9a:16:03:01:03. Independent of the weak
                // prefix-only check below (which keeps its own !ieHit guard), so
                // the two never double-count.
                // DS Parameter Set: the transmitter's own channel. Present on
                // beacons/probe responses (and some probe requests); a client's
                // probe request typically has none, so it keeps rx_ctrl.channel
                // below (band is still right, "heard while tuned to" this chan).
                if (id == 3 && elen == 1) {
                    dsCh = body[b+2];
                }

                if (id == 221 && elen == 7 &&
                    body[b+2] == 0x50 && body[b+3] == 0x6F && body[b+4] == 0x9A &&
                    body[b+5] == 0x16 && body[b+6] == 0x03 &&
                    body[b+7] == 0x01 && body[b+8] == 0x03) {
                    liteonSig = true;
                }

                if (!ieHit && id == 221 && elen >= 4 && body[b+2] == 0x50 && body[b+3] == 0x6F && body[b+4] == 0x9A) {
                    ieHit = true;
                    wifiConfidence += W_WIFI_IE;
                    // Deliberately does NOT set a category: 50:6F:9A is Lite-On's
                    // general OUI, present in countless consumer WiFi chips. It
                    // is a weak corroborating hint, not a vendor identification.
                    if (W_WIFI_IE > bestWeight) { bestWeight = W_WIFI_IE; matchedRule = "Lite-On Vendor IE (weak)"; }
                }

                // Drone Remote ID over WiFi Beacon: ASTM (90:3A:E6) or French
                // (FA:0B:BC) vendor IE, decoded in full.
                if (id == 221 && elen >= 6 &&
                    ((body[b+2] == 0x90 && body[b+3] == 0x3A && body[b+4] == 0xE6) ||
                     (body[b+2] == 0xFA && body[b+3] == 0x0B && body[b+4] == 0xBC))) {
                    odidHeard = true;
                    wifiConfidence += W_DRONE;
                    if (W_DRONE > bestWeight) {
                        bestWeight = W_DRONE;
                        matchedRule = "Remote ID Drone";
                        matchedCategory = "Drone";
                    }
                    // The ODID payload starts after OUI (3) + vendor type (1) +
                    // message counter (1). Length comes from this element's own
                    // elen — Sky Sweeper used "everything to end of frame",
                    // which fed the decoder every subsequent IE plus the FCS as
                    // if it were drone payload.
                    int odidLen = (int)elen - 5;
                    if (odidLen > 0) {
                        memset(&wifiUas, 0, sizeof(wifiUas));
                        odidDecodeInto(wifiUas, body + b + 7, (size_t)odidLen);
                        droneDecoded = odidUseful(wifiUas);
                    }
                }

                // A zero-length SSID IE on a probe request is a wildcard probe:
                // "any AP, answer me". Meaningless alone — every phone does it
                // — and the whole signal is that it came from a Flock OUI.
                if (id == 0 && elen == 0 && fsubtype == 4) wildcardSsid = true;

                if (pwnScan && id == 222 && elen > 0 &&
                    pwnLen + (int)elen < (int)sizeof(pwngridBuf)) {
                    memcpy(pwngridBuf + pwnLen, body + b + 2, elen);
                    pwnLen += (int)elen;
                }
                if (pwnScan && id == 223 && elen >= 1 && body[b+2] == 0x01) pwnGzip = true;

                if (id == 0 && elen > 0 && elen <= 32) {
                    char ssid[33] = {0};
                    memcpy(ssid, body + b + 2, elen);
                    // Keep the name as broadcast, for display. Only beacons and
                    // probe responses advertise the sender's own network; a
                    // probe request names the network a client is hunting for,
                    // which says nothing about the device sending it.
                    if (fsubtype == 8 || fsubtype == 5) foundSsid = String(ssid);
                    String ssidStr = String(ssid);
                    ssidStr.toLowerCase();
                    // "flock-" as a PREFIX: the Falcon's own hotspot is "Flock-" +
                    // the MAC's last 6 chars (WifiApService, 2026-09-21 firmware
                    // dump), and a bare substring matched home routers named
                    // "FlockNation" (TP-Link, in our own captures). Exactly 12
                    // chars because the suffix's colon format is unconfirmed but
                    // its length is not. "fs_" / "pigvision" were BLE *name*
                    // patterns, never SSIDs: as substrings they matched
                    // "Chiefs_Guest" and logged a Ubiquiti AP as an ALPR.
                    if (!ssidHit && ssidStr.length() == 12 && ssidStr.startsWith("flock-")) {
                        ssidHit = true;
                        wifiConfidence += W_WIFI_SSID;
                        if (W_WIFI_SSID > bestWeight) {
                            bestWeight = W_WIFI_SSID;
                            matchedRule = "Flock SSID Signature";
                            matchedCategory = "Flock Safety";   // SSID match IS vendor-specific
                        }
                    }
                }

                b += 2 + elen;
            }

            // The IE 50:6f:9a:16:03:01:03 is NOT a Flock camera on its own. It
            // alerted standalone for a while and caught passers-by: 2026-09-12
            // captures found it on AzureWave, China Dragon and Guangzhou Shiyuan
            // consumer modules (public MACs, -83..-92 dBm, one or two probes
            // each). 50:6f:9a is the Wi-Fi Alliance OUI, and 16 / 03 01 03 reads
            // as the MBO "cellular data capability" attribute, which any MBO
            // client may send. So it only tightens the listed-Flock-OUI wildcard
            // probe, the way upstream uses it. Random-MAC cameras need a real
            // tell from a capture at a pole (operator's phone WiFi off) first.
            if (flockOui && wildcardSsid) {
                int w = liteonSig ? W_WIFI_IE_SIG : W_WIFI_PROBE;
                wifiConfidence += w;
                if (w > bestWeight) {
                    bestWeight = w;
                    matchedRule = liteonSig ? "Flock probe + IE" : "Flock wildcard probe";
                    matchedCategory = "Flock Safety";
                }
            }

            // Pwnagotchi: the reassembled IE-222 payload is the advertisement —
            // raw JSON, or gzip when IE 223 = 1. A real advert normally compresses
            // (confirmed from pwngrid v1.10.3 source), so the gzip path is the
            // common one in the field; the raw path is what our bench emitter and
            // the `raw` scapy beacon send. pwngrid-specific fields are the
            // discriminator — the source MAC alone proves nothing (it is
            // editable). A garbage/partial parse just fails and never alerts.
            if (pwnScan && pwnLen > 0) {
                const char* json = nullptr;
                if (pwnGzip) {
#ifdef PWN_HAS_GUNZIP
                    // gzip = 10-byte header (pwngrid's writer sets no optional
                    // fields), raw DEFLATE, 8-byte trailer. Strip the header and
                    // inflate; tinfl stops at the DEFLATE end and ignores the
                    // trailer. NON_WRAPPING output = our buffer holds it all.
                    if (pwnLen > 18 && (uint8_t)pwngridBuf[0] == 0x1F &&
                                        (uint8_t)pwngridBuf[1] == 0x8B) {
                        tinfl_init(&pwnInflator);
                        size_t inSz  = (size_t)(pwnLen - 10);
                        size_t outSz = sizeof(pwnJson) - 1;
                        tinfl_status st = tinfl_decompress(&pwnInflator,
                            (const mz_uint8*)(pwngridBuf + 10), &inSz,
                            (mz_uint8*)pwnJson, (mz_uint8*)pwnJson, &outSz,
                            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                        if (st == TINFL_STATUS_DONE) { pwnJson[outSz] = 0; json = pwnJson; }
                    }
#endif
                    // (PWN_HAS_GUNZIP is now defined on both boards -- see the
                    // include block at the top of this file.)
                } else {
                    pwngridBuf[pwnLen] = 0;
                    json = pwngridBuf;
                }
                JsonDocument pdoc;
                if (json && deserializeJson(pdoc, json) == DeserializationError::Ok &&
                    (pdoc["pwnd_tot"].is<long>() || pdoc["pwnd_run"].is<long>() ||
                     !pdoc["grid_version"].isNull())) {
                    long tot = pdoc["pwnd_tot"] | (pdoc["pwnd_run"] | -1L);
                    const char* nm = pdoc["name"] | "";
                    char clean[25]; int ci = 0;
                    for (int i = 0; nm[i] && ci < (int)sizeof(clean) - 1; i++)
                        if (nm[i] >= 0x20 && nm[i] != '"' && nm[i] != '\\' && (uint8_t)nm[i] < 0x7F)
                            clean[ci++] = nm[i];
                    clean[ci] = 0;
                    char rbuf[64];
                    if (tot >= 0) snprintf(rbuf, sizeof(rbuf), "Pwnagotchi '%s' - %ld pwned", clean, tot);
                    else          snprintf(rbuf, sizeof(rbuf), "Pwnagotchi '%s'", clean);
                    int w = pwnMac ? W_PWNGRID + W_CORROBORATION : W_PWNGRID;
                    if (w > 100) w = 100;
                    wifiConfidence += w;
                    if (w > bestWeight) {
                        bestWeight = w;
                        matchedRule = String(rbuf);
                        matchedCategory = HACKING_GEAR_CATEGORY;
                    }
                }
            }
        }

        // Karma / PineAP: one BSSID (addr2) sending PROBE RESPONSES for many
        // distinct SSIDs is "impersonate all networks". A legit multi-SSID AP
        // uses distinct BSSIDs per SSID, so this fans out from one. Attack only.
        if (attackDetect && fsubtype == 5 && foundSsid.length() > 0) {
            uint64_t sh = attackHash((const uint8_t*)foundSsid.c_str(), foundSsid.length());
            uint32_t nSsids = karmaNote(&karmaState, attackMac48(addr2), sh, millis());
            if (nSsids >= ATTACK_KARMA_THRESHOLD) {
                char m[18]; snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
                    addr2[0], addr2[1], addr2[2], addr2[3], addr2[4], addr2[5]);
                char title[40]; snprintf(title, sizeof(title), "%s \xC2\xB7 %u SSIDs",
                    ATTACK_TITLE_KARMA, (unsigned)nSsids);
                upsertAttackTarget(String(m), rssi, "WiFi", String(title), packet->rx_ctrl.channel);
            }
        }

        // SSID-prefix rules (Axon body cams broadcast AB3-... while pairing).
        // foundSsid is set only for beacons and probe responses: a probe
        // request names the network a client wants, not the sender. Same
        // bounded lock as the OUI loop above; first matching rule wins.
        if (foundSsid.length() > 0 &&
            xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            for (const auto& sig : loadedSignatures) {
                if (sig.ssidPrefix.length() == 0 || ruleGatedOff(sig) ||
                    !foundSsid.startsWith(sig.ssidPrefix)) continue;
                // A rule with an OUI is still an AND: the SSID prefix alone
                // isn't enough, the frame's own source MAC must also start
                // with it. Reuses the cleanMac built above for the OUI loop.
                if (sig.oui.length() > 0) {
                    String cleanOui = "";
                    for (size_t i = 0; i < sig.oui.length(); i++) {
                        char c = sig.oui[i];
                        if (c != ':' && c != '-') cleanOui += (char)toupper(c);
                    }
                    if (!cleanMac.startsWith(cleanOui)) continue;
                }
                // Same principle as the OUI loop: mfg_id/device_name/
                // service_uuid are BLE-only conditions this callback can't
                // check, so a rule that states any of them can't have its own
                // weight applied here -- fall back to the generic SSID weight.
                bool ouiOnly = sig.mfgId.length() == 0 && sig.deviceName.length() == 0 && sig.serviceUuid.length() == 0;
                int w = (ouiOnly && sig.weight > 0) ? sig.weight : W_WIFI_SSID;
                wifiConfidence += w;
                if (w > bestWeight) {
                    bestWeight = w;
                    matchedRule = sig.name.length() > 0 ? sig.name : "SSID prefix match";
                    matchedCategory = sig.category;
                }
                break;
            }
            xSemaphoreGive(watchersMutex);
        }

        if (wifiConfidence > 100) wifiConfidence = 100;

        // Same as the BLE path: harvest everything, score is an annotation.
        if (xSemaphoreTake(watchersMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            bool found = false;
            uint32_t now = millis();
            for (auto& target : trackedTargets) {
                if (target.mac.equalsIgnoreCase(mac)) {
                    target.rssi = rssi;
                    target.lastSeenMs = now;
                    target.count++;
                    int merged = wifiConfidence;
                    // Corroboration: this MAC was previously seen over BLE.
                    if (target.protocol == "BLE" || target.protocol == "BLE+WiFi") {
                        merged = wifiConfidence + W_CORROBORATION;
                        target.protocol = "BLE+WiFi";
                    } else {
                        target.protocol = "WiFi";
                    }
                    if (merged > 100) merged = 100;
                    if (merged > target.confidence) target.confidence = merged;
                    target.tier = tierForConfidence(target.confidence);
                    // Label by what actually matched. The old code stamped
                    // "Flock Safety" on every WiFi hit, so a Cradlepoint router
                    // or a stray ESP32 was reported as a Flock camera.
                    if (matchedRule.length() > 0) {
                        target.type = matchedCategory;
                        target.matchedRule = matchedRule;
                    }
                    if (foundSsid.length() > 0) target.ssid = foundSsid;
                    if (role > target.wifiRole) target.wifiRole = role;
                    target.wifiCh = dsCh > 0 ? dsCh : packet->rx_ctrl.channel;
                    if (droneDecoded) applyDroneData(target, wifiUas);
                    noteAlertForTarget(target, bestWeight, matchedCategory);
                    found = true;
                    break;
                }
            }
            if (!found) {
                WatcherTargetInfo newTarget;
                newTarget.mac = mac;
                newTarget.name = "";
                newTarget.type = matchedCategory;
                newTarget.matchedRule = matchedRule;
                newTarget.rssi = rssi;
                newTarget.firstSeenMs = now;
                newTarget.lastSeenMs = now;
                newTarget.count = 1;
                newTarget.protocol = "WiFi";
                newTarget.ssid = foundSsid;
                newTarget.wifiRole = role;
                newTarget.wifiCh = dsCh > 0 ? dsCh : packet->rx_ctrl.channel;
                newTarget.confidence = wifiConfidence;
                newTarget.tier = tierForConfidence(wifiConfidence);
                newTarget.lastReportedMs = 0;
                if (droneDecoded) applyDroneData(newTarget, wifiUas);
                auto* added = appendTrackedTarget(newTarget);

                if (wifiConfidence >= CONF_LIST_MIN) {
                    ESP_LOGI(TAG, "[SURVEILLANCE - WIFI] MAC: %s, Rule: %s, Conf: %d (%s), RSSI: %d",
                             mac.c_str(), matchedRule.c_str(), wifiConfidence, newTarget.tier.c_str(), rssi);
                }
                if (added) noteAlertForTarget(*added, bestWeight, matchedCategory);
            }
            xSemaphoreGive(watchersMutex);
        }
    }
}

