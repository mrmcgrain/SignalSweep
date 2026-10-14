// SPDX-License-Identifier: GPL-3.0-or-later
#include "cardputer_ui.h"
#ifdef SIGNALSWEEP_CARDPUTER_ADV
#include <M5Cardputer.h>
#include <algorithm>
#include "mode_watchers_watch.h"
#include "hardware_manager.h"
#include "ble_serial.h"
#include "sd_store.h"
#include "alert_log.h"
#include "scanner_view.h"
#include "sd_log.h"
#include "host_time.h"
#include "mode_capture.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>

namespace {
constexpr size_t ROW_CAPACITY = 24;
constexpr uint16_t BG = 0x0841, FG = 0xFFFF, DIM = 0x9CD3, GREEN = 0x47D0, AMBER = 0xFD60;
ScannerRow rows[ROW_CAPACITY] = {};
size_t rowCount = 0, total = 0, selected = 0;
String selectedMac;
enum Page { LIST, DETAIL, SETTINGS, CAPTURE };
Page page = LIST;
const uint32_t captureDurations[] = {15, 60, 300, 0};
unsigned captureDuration = 1;
bool wasCapturing = false;
uint32_t snapshotAt = 0, pollAt = 0, paintAt = 0, batteryAt = 0;
uint32_t oldKeys = 0;
uint32_t keyEvents = 0, lastKeyMask = 0, renderedFrames = 0;
int battery = -1;
bool dirty = true, snapshotGood = false;
M5Canvas canvas(&M5Cardputer.Display);
bool canvasReady = false;

String clean(const char* text, size_t n) { return String(text).substring(0, n); }
const char* sdLabel() {
    switch (sdState()) {
        case SD_OK: return "OK";
        case SD_FULL: return "FULL";
        case SD_ERROR: return "ERROR";
        default: return "NO CARD";
    }
}
void text(lgfx::LGFXBase& g, int x, int y, const String& s, uint16_t c = FG) {
    g.setTextColor(c, BG); g.setCursor(x, y); g.print(s);
}
bool stale() { return !snapshotGood || scanner_view::expired(millis(), snapshotAt, 3000); }
void refreshRows() {
    size_t count = rowCount, found = total;
    if (!getScannerRows(rows, ROW_CAPACITY, count, found)) return;
    rowCount = count; total = found; snapshotGood = true; snapshotAt = millis();
    // A new stronger signal must not change which device Enter/H acts on.
    size_t index = scanner_view::selectedIndex(rows, rowCount, selectedMac.c_str());
    if (index < rowCount) selected = index;
    else {
        selected = 0;
        selectedMac = rowCount ? rows[0].mac : "";
        if (page == DETAIL) page = LIST;
    }
    dirty = true;
}
void moveSelection(int delta) {
    if (!rowCount) return;
    selected = (selected + rowCount + delta) % rowCount;
    selectedMac = rows[selected].mac;
    dirty = true;
}
void huntSelected() {
    if (!rowCount || stale()) return;
    uint8_t mac[6];
    if (!alertLogParseMac(selectedMac.c_str(), mac)) return; // flood summary is not a device
    setHuntTarget(getHuntTarget().equalsIgnoreCase(selectedMac) ? "" : selectedMac);
    page = DETAIL;
}
void render() {
    // Recover the optional frame buffer if runtime radio/JSON allocations
    // leave little internal RAM. Direct drawing uses no frame allocation.
    if (canvasReady && ESP.getFreeHeap() < 40960) {
        canvas.deleteSprite(); canvasReady = false;
    }
    lgfx::LGFXBase& g = canvasReady ? static_cast<lgfx::LGFXBase&>(canvas)
                                  : static_cast<lgfx::LGFXBase&>(M5Cardputer.Display);
    g.startWrite();
    g.fillScreen(BG); g.setTextSize(1); g.setTextWrap(false);
    text(g, 4, 3, "SignalSweep / ADV", GREEN);
    text(g, 181, 3, battery < 0 ? "BAT ?" : String(battery) + "%", DIM);
    g.drawFastHLine(0, 15, 240, 0x3186);
    String status = String(isBleScanEnabled() ? "BLE+ " : "BLE- ") +
                    (isWifiScanEnabled() ? "2.4G+ " : "2.4G- ") +
                    (isBuzzerEnabled() ? "SOUND " : "MUTE ") +
                    (getAlertLogEnabled() ? String("LOG:") + sdLabel() : String("LOG:OFF"));
    text(g, 4, 19, status, DIM);
    if (page == CAPTURE) {
        CaptureStatus cap = getCaptureStatus();
        text(g, 4, 34, cap.active ? "CAPTURING / detector paused" : cap.pending ? "Starting capture..." : "Packet capture / SD", GREEN);
        text(g, 4, 46, cap.file[0] ? String("File: ") + cap.file : "/SIGSWEEP/CAPnnnnn.SSC", DIM);
        text(g, 4, 58, "Wi-Fi " + String(cap.wifi) + "  BLE " + String(cap.ble));
        text(g, 4, 70, "Drops " + String(cap.drops) + "  Records " + String(cap.written), cap.drops ? AMBER : FG);
        uint32_t secs = captureDurations[captureDuration];
        text(g, 4, 82, cap.active ? (cap.remain ? String(cap.remain) + " seconds remaining" : "Until stopped")
                                : (secs ? String("D Duration: ") + secs + " seconds" : "D Duration: until stopped"));
        text(g, 4, 94, cap.error ? String("Error: ") + cap.error : "256-byte prefixes / passive BLE", cap.error ? AMBER : DIM);
        text(g, 4, 106, cap.active ? "Enter STOP / save / resume scan" : "Enter START to SD  C list");
        text(g, 4, 123, "Back stop + list  M mute", DIM);
    } else if (page == SETTINGS) {
        text(g, 4, 34, String("M Sound: ") + (isBuzzerEnabled() ? "on" : "muted"));
        text(g, 4, 46, String("A List: ") + (getScanAll() ? "all signals" : "matches only"));
        String log = !getAlertLogEnabled() ? "off" : sdState() == SD_OK ? "on / SD ready" : "on / FLASH ONLY";
        text(g, 4, 58, "L Alert logging: " + log, sdState() == SD_OK ? FG : AMBER);
        text(g, 4, 70, "V Vol " + String(getVolume() + 1) + "/3 SD:" + sdLabel() + " R retry");
        text(g, 4, 82, String("Q Receive-only: ") + (getRxOnly() ? "on" : "off"));
        text(g, 4, 94, "B BLE / W Wi-Fi on/off");
        text(g, 4, 106, "C Packet capture to SD", GREEN);
        text(g, 4, 123, "Tab list  X stop hunt", DIM);
    } else if (page == DETAIL && rowCount) {
        const ScannerRow& r = rows[selected];
        text(g, 4, 34, clean(r.name, 38), GREEN);
        text(g, 4, 46, clean(r.mac, 38), DIM);
        text(g, 4, 58, clean(r.type, 26) + " / " + r.protocol);
        text(g, 4, 70, "Rule: " + clean(r.rule, 30));
        text(g, 4, 82, "Score " + String(r.confidence) + "  Ch " + String(r.channel));
        bool heard = millis() - r.lastSeenMs <= 8000 && !stale();
        text(g, 4, 94, heard ? String(r.rssi) + " dBm" : "Signal lost / stale", heard ? FG : AMBER);
        g.drawRect(4, 107, 150, 8, DIM);
        if (heard) g.fillRect(5, 108, constrain((r.rssi + 100) * 148 / 70, 0, 148), 6, GREEN);
        bool hunting = getHuntTarget().equalsIgnoreCase(selectedMac);
        text(g, 163, 106, hunting ? "HUNTING" : "H hunt", hunting ? AMBER : DIM);
        text(g, 4, 123, "Back list  H hunt  X stop  M mute", DIM);
    } else {
        String filter = getScanAll() ? "All signals " : "Matches ";
        text(g, 4, 34, filter + String(rowCount) + "/" + String(total) +
             "  Alerts " + String(getAlertCount()), stale() ? AMBER : DIM);
        if (!rowCount) {
            text(g, 12, 61, stale() ? "Waiting for scanner..." : "Listening for matching signals");
            text(g, 12, 80, "A show all   Tab settings", DIM);
        }
        size_t first = selected / 5 * 5;
        for (size_t i = first; i < rowCount && i < first + 5; ++i) {
            int y = 48 + (i - first) * 14;
            if (i == selected) g.fillRect(0, y - 2, 240, 13, 0x21A5);
            text(g, 4, y, i == selected ? ">" : " ", GREEN);
            text(g, 14, y, clean(rows[i].name, 26), i == selected ? GREEN : FG);
            text(g, 188, y, String(rows[i].rssi), DIM);
        }
        text(g, 4, 123, stale() ? "STALE / C capture / Tab setup" : "K/J select Enter detail C capture", stale() ? AMBER : DIM);
    }
    g.endWrite();
    if (canvasReady) canvas.pushSprite(0, 0);
    ++renderedFrames;
}
uint32_t keys() {
    auto& k = M5Cardputer.Keyboard.keysState();
    uint32_t mask = 0;
    if (M5Cardputer.Keyboard.isKeyPressed('k') || M5Cardputer.Keyboard.isKeyPressed(';')) mask |= 1u << 0;
    if (M5Cardputer.Keyboard.isKeyPressed('j') || M5Cardputer.Keyboard.isKeyPressed('.')) mask |= 1u << 1;
    if (k.enter) mask |= 1u << 2;
    if (k.tab) mask |= 1u << 3;
    if (k.del) mask |= 1u << 4;
    const char* actions = "hxmalrvqbwcd";
    // Explicit mapping shared only with the switch below; space is not an action.
    for (unsigned i = 0; actions[i]; ++i) {
        char c = actions[i];
        if (c != ' ' && M5Cardputer.Keyboard.isKeyPressed(c)) mask |= 1u << (5 + i);
    }
    return mask;
}
void handleKeys(uint32_t pressed) {
    if (pressed) { ++keyEvents; lastKeyMask = pressed; dirty = true; }
    if (pressed & (1u << 15)) { page = page == CAPTURE && !isCapturing() ? LIST : CAPTURE; return; } // c
    if (page == CAPTURE) {
        if (pressed & (1u << 2)) {
            if (isCapturing()) stopCapture();
            else requestCapture(captureDurations[captureDuration], true);
        }
        if ((pressed & (1u << 16)) && !isCapturing()) captureDuration = (captureDuration+1)%4; // d
        if (pressed & (1u << 4)) { stopCapture(); page = LIST; }
        if (pressed & (1u << 7)) setBuzzerEnabled(!isBuzzerEnabled());
        return;
    }
    if (isCapturing()) { page=CAPTURE; return; }
    if (pressed & (1u << 0)) moveSelection(-1);
    if (pressed & (1u << 1)) moveSelection(1);
    if (pressed & (1u << 2)) { if (rowCount && !stale()) page = page == DETAIL ? LIST : DETAIL; }
    if (pressed & (1u << 3)) page = page == SETTINGS ? LIST : SETTINGS;
    if (pressed & (1u << 4)) page = LIST;
    if (pressed & (1u << 5)) huntSelected();          // h
    if (pressed & (1u << 6)) setHuntTarget("");        // x
    if (pressed & (1u << 7)) setBuzzerEnabled(!isBuzzerEnabled()); // m
    if (pressed & (1u << 8)) { setScanAll(!getScanAll()); refreshRows(); } // a
    if (pressed & (1u << 9)) setAlertLogEnabled(!getAlertLogEnabled()); // l
    if (pressed & (1u << 10)) sdProbe();             // r; loop context, not radio task
    if (pressed & (1u << 11)) setVolume((getVolume() + 1) % 3); // v
    if (pressed & (1u << 12)) processIncomingCommand(getRxOnly() ? "CMD:RXONLY:OFF" : "CMD:RXONLY:ON"); // q
    if (pressed & (1u << 13)) processIncomingCommand(isBleScanEnabled() ? "CMD:BLE_SCAN:OFF" : "CMD:BLE_SCAN:ON"); // b
    if (pressed & (1u << 14)) processIncomingCommand(isWifiScanEnabled() ? "CMD:WIFI_SCAN:OFF" : "CMD:WIFI_SCAN:ON"); // w
}
} // namespace

void cardputerUiInit() {
    auto cfg = M5.config();
    cfg.fallback_board = m5::board_t::board_M5CardputerADV;
    cfg.internal_mic = false; cfg.internal_imu = false; cfg.internal_rtc = false;
    cfg.external_display_value = 0; cfg.external_imu = false; cfg.external_rtc = false;
    cfg.internal_spk = true;
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(100);
    M5Cardputer.Speaker.setVolume(100);
    canvas.setColorDepth(8);
    canvasReady = canvas.createSprite(240, 135) != nullptr;
    Serial.printf("[ADV] board=%d display=%dx%d UI=%s PSRAM=%u\n",
        (int)M5.getBoard(), M5Cardputer.Display.width(), M5Cardputer.Display.height(),
        canvasReady ? "buffered" : "direct", ESP.getPsramSize());
    render();
}

void cardputerUiTick() {
    M5Cardputer.update();
    uint32_t held = keys();
    handleKeys(scanner_view::keyPresses(held, oldKeys)); // transitions, even when one key replaces another
    oldKeys = held;
    uint32_t now = millis();
    bool capturing = isCapturing();
    if (capturing && !wasCapturing) page = CAPTURE;
    wasCapturing = capturing;
    if (now - pollAt >= 500) {
        pollAt = now;
        if (!capturing) refreshRows();
        else dirty = true;
    }
    if (batteryAt == 0 || now - batteryAt >= 10000) {
        batteryAt = now; battery = M5Cardputer.Power.getBatteryLevel();
        dirty = true;
    }
    if ((dirty || stale()) && now - paintAt >= 250) {
        paintAt = now; dirty = false; render();
    }
}
void cardputerUiPrepareCapture() {
    if (canvasReady) { canvas.deleteSprite(); canvasReady = false; }
    dirty = true;
}
// Foreground USB diagnostics only: no radio callback may do SD/display I/O.
bool cardputerHandleUsbCommand(const String& cmd) {
    if (!cmd.startsWith("CMD:ADV:")) return false;
    JsonDocument doc;
    if (cmd == "CMD:ADV:STATUS") {
        doc["adv"] = true; doc["board"] = (int)M5.getBoard();
        doc["width"] = M5Cardputer.Display.width(); doc["height"] = M5Cardputer.Display.height();
        doc["page"] = (int)page; doc["frames"] = renderedFrames;
        doc["buffered"] = canvasReady; doc["key_events"] = keyEvents; doc["last_key_mask"] = lastKeyMask;
        doc["speaker_running"] = M5Cardputer.Speaker.isRunning();
        doc["speaker_playing"] = M5Cardputer.Speaker.isPlaying();
        doc["heap"] = ESP.getFreeHeap(); doc["min_heap"] = ESP.getMinFreeHeap();
        doc["largest_heap_block"] = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        doc["sd"] = sdState(); doc["logging"] = getAlertLogEnabled();
        doc["capture_supported"] = true;
        CaptureStatus cap = getCaptureStatus();
        doc["capturing"] = cap.active; doc["capture_pending"] = cap.pending;
        doc["capture_file"] = cap.file; doc["capture_drops"] = cap.drops;
        doc["capture_written"] = cap.written;
        if (cap.error) doc["capture_error"] = cap.error;
        const esp_partition_t* run = esp_ota_get_running_partition();
        if (run) { doc["app_offset"] = run->address; doc["app_label"] = run->label; }
    } else if (cmd == "CMD:ADV:SCREEN") {
        if (!canvasReady) { doc["error"] = "screen buffer not available; inspect physical LCD"; }
        else {
            sendUsbLine("ADVSCREEN:BEGIN:240:135:RGB565LE");
            uint8_t pixels[480], encoded[644];
            for (int y = 0; y < 135; ++y) {
                for (int x = 0; x < 240; ++x) {
                    uint16_t color = canvas.readPixel(x, y);
                    pixels[x*2] = color & 255; pixels[x*2+1] = color >> 8;
                }
                size_t bytes = 0;
                mbedtls_base64_encode(encoded, sizeof(encoded)-1, &bytes, pixels, sizeof(pixels));
                encoded[bytes] = 0;
                sendUsbLine(String("ADVPIX:") + y + ":" + (const char*)encoded);
                delay(1);
            }
            sendUsbLine("ADVSCREEN:END");
            return true;
        }
    } else if (cmd.startsWith("CMD:ADV:SPEAKER:")) {
        String value = cmd.substring(16);
        if (value.length() != 1 || value[0] < '0' || value[0] > '4') doc["error"] = "category must be 0..4";
        else { triggerCategoryAlert((AlertCategory)(value[0]-'0')); doc["speaker_test"] = value.toInt(); doc["muted"] = !isBuzzerEnabled(); }
    } else if (cmd == "CMD:ADV:SDTEST") {
        if (isCapturing()) { sendUsbLine("{\"error\":\"stop capture before SD test\"}"); return true; }
        if (sdState() != SD_OK) sdProbe();
        if (sdState() != SD_OK) doc["error"] = "SD card not mounted";
        else {
            // A requested, visibly synthetic row through the actual alert CSV writer.
            SdLogFields f = {};
            f.secs = alertLogSecs(); f.mac[0] = 0x02; f.mac[5] = 0xfe;
            f.cat = ALERT_GENERIC; f.rssi = -127; f.rule = "TEST ONLY";
            f.name = "Synthetic SD logger test"; f.company = -1; f.confidence = 0;
            String filename = sdLogFileName(alertLogBoot());
            uint32_t before = 0; uint8_t one;
            sdRead(filename.c_str(), 0, &one, 1, &before);
            String expected = sdLogCsvLine(alertLogBoot(), f, hostTimeNow());
            sdLogWrite(f);
            uint32_t after = 0;
            sdRead(filename.c_str(), 0, &one, 1, &after);
            uint8_t tail[256];
            int got = after >= expected.length() && expected.length() <= sizeof(tail)
                ? sdRead(filename.c_str(), after - expected.length(), tail, expected.length(), nullptr) : -1;
            doc["sd_test"] = got == (int)expected.length() && !memcmp(tail, expected.c_str(), expected.length());
            doc["synthetic"] = true; doc["file"] = filename; doc["bytes_before"] = before; doc["bytes_after"] = after;
            doc["sd"] = sdState();
        }
    } else if (cmd == "CMD:ADV:REBOOT") {
        sendUsbLine("{\"adv_reboot\":true}"); delay(100); ESP.restart();
    } else { doc["error"] = "unknown ADV diagnostic"; }
    String reply; serializeJson(doc, reply); sendUsbLine(reply); return true;
}
#endif
