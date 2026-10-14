// SPDX-License-Identifier: GPL-3.0-or-later
#if defined(SIGNALSWEEP_CARDPUTER_ADV) && !defined(SIGNALSWEEP_NO_CAPTURE)
#ifdef SIGNALSWEEP_CAPTURE_HOST_TEST
#include "../test/capture_stubs/platform.h"
#else
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <NimBLEDevice.h>
#include <mbedtls/base64.h>
#include "mode_watchers_watch.h"
#include "mode_manager.h"
#include "sd_store.h"
#include "host_time.h"
#include "ble_serial.h"
#include "cardputer_ui.h"
#endif
#include <time.h>
#include "mode_capture.h"
#include "capture_buffer.h"

namespace {
// ~11 KB total, no allocation/free in either radio callback. No extra tasks.
capture_buffer::Queue<32> wifi;
capture_buffer::Queue<8> ble;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
CaptureStatus status = {};
bool stopWanted = false;
bool starting = false, restorePending = false, requestedSd = false;
uint32_t requestedSecs = 0, startedAt = 0, durationMs = 0, hopAt = 0, statAt = 0;
uint8_t channel = 1;
uint8_t sdBuffer[2048];
size_t sdUsed = 0;

void reply(const char* s) { sendUsbLine(String(s)); sendBleSerial(String(s)); }
void fail(const char* error) {
    portENTER_CRITICAL(&mux); status.error = error; portEXIT_CRITICAL(&mux);
}
void flush() {
    if (!sdUsed) return;
    if (!status.error && !sdStreamWrite(sdBuffer, sdUsed))
        fail(sdState() == SD_FULL ? "card full" : "card lost");
    sdUsed = 0;
}
void emit(const capture_buffer::Record& record) {
    if (status.error) return;
    uint8_t raw[capture_buffer::HEADER_BYTES+capture_buffer::MAX_BYTES], b64[368];
    size_t n = capture_buffer::encode(record, raw), encoded = 0;
    if (mbedtls_base64_encode(b64, sizeof(b64), &encoded, raw, n) != 0) {
        fail("encode failed"); return;
    }
    if (status.toSd) {
        if (sdUsed+encoded+1 > sizeof(sdBuffer)) flush();
        if (status.error) return;
        memcpy(sdBuffer+sdUsed, b64, encoded); sdUsed += encoded;
        sdBuffer[sdUsed++] = '\n';
    } else sendUsbLine("CAP:", b64, encoded);
    portENTER_CRITICAL(&mux); ++status.written; portEXIT_CRITICAL(&mux);
}
void drain(unsigned budget) {
    for (unsigned i = 0; i < budget; ++i) {
        capture_buffer::Record record;
        // Alternate queues so a Wi-Fi flood cannot starve BLE.
        portENTER_CRITICAL(&mux);
        bool got = i%2 ? (ble.pop(record) || wifi.pop(record))
                       : (wifi.pop(record) || ble.pop(record));
        portEXIT_CRITICAL(&mux);
        if (!got) break;
        emit(record);
    }
}
void wifiCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    // WIFI_PKT_MISC has no payload even when sig_len is nonzero.
    if (!buf || type == WIFI_PKT_MISC) return;
    auto* p = static_cast<wifi_promiscuous_pkt_t*>(buf);
    if (p->rx_ctrl.rx_state || !p->rx_ctrl.sig_len) return;
    portENTER_CRITICAL(&mux);
    if (status.active) wifi.push(0, micros(), p->rx_ctrl.channel,
        p->rx_ctrl.rssi, p->payload, p->rx_ctrl.sig_len, p->rx_ctrl.sig_len);
    portEXIT_CRITICAL(&mux);
}
class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        uint8_t data[7+62];
        NimBLEAddress address = dev->getAddress();
        const uint8_t* mac = address.getNative();
        for (unsigned i=0; i<6; ++i) data[i] = mac[5-i];
        data[6] = address.getType();
        size_t original = dev->getPayloadLength();
        size_t len = original > 62 ? 62 : original;
        memcpy(data+7, dev->getPayload(), len);
        portENTER_CRITICAL(&mux);
        if (status.active) ble.push(1, micros(), 0, dev->getRSSI(), data, len+7, original+7);
        portEXIT_CRITICAL(&mux);
    }
} callbacks;
void report(bool done) {
    CaptureStatus s = getCaptureStatus();
    char msg[220];
    snprintf(msg, sizeof(msg),
        "{\"cap\":{\"wifi\":%u,\"ble\":%u,\"drops\":%u,\"written\":%u,\"remain\":%u,\"done\":%s}}",
        unsigned(s.wifi), unsigned(s.ble), unsigned(s.drops), unsigned(s.written),
        unsigned(s.remain), done ? "true" : "false");
    reply(msg);
}
void restoreDetector() {
    startWatchersWatch();
    pauseBle(!isBleScanEnabled()); pauseWifi(!isWifiScanEnabled());
}
void finish() {
    portENTER_CRITICAL(&mux); starting = true; status.active = false; portEXIT_CRITICAL(&mux);
    esp_wifi_set_promiscuous(false); esp_wifi_set_promiscuous_rx_cb(nullptr);
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->stop(); scan->clearResults();
    drain(40); // all queued records, producers can no longer enqueue
    if (status.toSd) {
        flush();
        if (!status.error) {
            CaptureStatus s=getCaptureStatus(); char end[140];
            int n=snprintf(end,sizeof(end),"#SSCAP END wifi=%u ble=%u drops=%u written=%u\n",
                unsigned(s.wifi),unsigned(s.ble),unsigned(s.drops),unsigned(s.written));
            if(!sdStreamWrite(reinterpret_cast<uint8_t*>(end),n)) fail("card lost");
            if(!status.error && !sdStreamSync()) fail("card sync failed");
        }
        sdStreamClose();
    }
    if (status.error) {
        char msg[260]; CaptureStatus s = getCaptureStatus();
        snprintf(msg,sizeof(msg),"{\"cap\":{\"error\":\"%s\",\"wifi\":%u,\"ble\":%u,\"drops\":%u,\"written\":%u,\"done\":false}}",
                 s.error,unsigned(s.wifi),unsigned(s.ble),unsigned(s.drops),unsigned(s.written));
        reply(msg);
    } else report(true);
    restoreDetector();
    portENTER_CRITICAL(&mux); starting = false; portEXIT_CRITICAL(&mux);
}
void begin(uint32_t secs, bool toSd) {
    portENTER_CRITICAL(&mux);
    status = {}; status.toSd = toSd; wifi.reset(); ble.reset();
    portEXIT_CRITICAL(&mux);
    sdUsed = 0;
    char filename[13] = "";
    if (toSd) {
        if (sdState()!=SD_OK) sdProbe();
        if (sdState()!=SD_OK || !sdNextCaptureName(filename) || !sdStreamOpen(filename)) {
            fail(sdState()==SD_NONE ? "no card" : sdState()==SD_FULL ? "card full" : "card open failed");
        } else {
            portENTER_CRITICAL(&mux); memcpy(status.file,filename,sizeof(filename)); portEXIT_CRITICAL(&mux);
            char header[128], t[24] = "-";
            time_t utc = hostTimeNow();
            if (utc) { struct tm tmv; gmtime_r(&utc,&tmv); strftime(t,sizeof(t),"%Y-%m-%dT%H:%M:%SZ",&tmv); }
            int n = snprintf(header,sizeof(header),"#SSCAP v1 secs=%u src=sd t=%s board=adv max_bytes=256\n",unsigned(secs),t);
            if (!sdStreamWrite(reinterpret_cast<uint8_t*>(header),n)) fail("card lost");
        }
        if (status.error) { sdStreamClose(); char msg[100]; snprintf(msg,sizeof(msg),"{\"cap\":{\"error\":\"%s\"}}",status.error); reply(msg); return; }
    }
    cardputerUiPrepareCapture(); // release optional LCD buffer before radio start
    stopWatchersWatch();
    // Detector hopper/telemetry tasks wake every 250 ms. Wait for their exit
    // before changing the radio or allowing a fast stop/start to resurrect them.
    uint32_t waiting = millis();
    while (!watchersTasksStopped() && uint32_t(millis()-waiting)<1500) delay(10);
    if (!watchersTasksStopped()) {
        fail("detector stop timeout"); if(toSd) sdStreamClose();
        reply("{\"cap\":{\"error\":\"detector stop timeout\"}}");
        // Resume from a later tick once the old tasks finally exit.
        portENTER_CRITICAL(&mux); restorePending = true; portEXIT_CRITICAL(&mux);
        return;
    }
    startedAt = statAt = hopAt = millis(); durationMs = secs*1000UL; channel = 1;
    WiFi.mode(WIFI_STA); WiFi.disconnect();
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_CTRL };
    if (esp_wifi_set_promiscuous_filter(&filter)!=ESP_OK ||
        esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK ||
        esp_wifi_set_promiscuous_rx_cb(wifiCallback)!=ESP_OK) {
        fail("Wi-Fi setup failed"); finish(); return;
    }
    portENTER_CRITICAL(&mux); status.active = true; portEXIT_CRITICAL(&mux);
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&callbacks,true);
    scan->setActiveScan(false); // passive advertisements, no scan requests
    scan->setMaxResults(0); scan->setInterval(100); scan->setWindow(40);
    if (esp_wifi_set_promiscuous(true)!=ESP_OK || !scan->start(0,nullptr,false)) {
        fail("radio start failed"); finish(); return;
    }
    char msg[160]; snprintf(msg,sizeof(msg),"{\"cap\":{\"started\":true,\"secs\":%u,\"file\":\"%s\",\"max_bytes\":256,\"wifi_slots\":32,\"ble_slots\":8}}",unsigned(secs),status.file);
    reply(msg);
}
} // namespace

void requestCapture(uint32_t secs, bool toSd) {
    if (secs>3600) { reply("{\"cap\":{\"error\":\"duration must be 0..3600 seconds\"}}"); return; }
    portENTER_CRITICAL(&mux);
    bool busy = status.active || status.pending || starting || restorePending;
    if (!busy) { requestedSecs=secs; requestedSd=toSd; stopWanted=false; status.pending=true; }
    portEXIT_CRITICAL(&mux);
    if(busy) reply("{\"cap\":{\"error\":\"busy\"}}");
}
void stopCapture() {
    portENTER_CRITICAL(&mux); stopWanted=true; status.pending=false; portEXIT_CRITICAL(&mux);
}
CaptureStatus getCaptureStatus() {
    portENTER_CRITICAL(&mux);
    CaptureStatus out = status;
    if(out.pending) out.toSd=requestedSd;
    out.pending = out.pending || starting || restorePending;
    out.wifi=wifi.seen-wifi.dropped; out.ble=ble.seen-ble.dropped;
    out.drops=wifi.dropped+ble.dropped;
    uint32_t start = startedAt, duration = durationMs;
    portEXIT_CRITICAL(&mux);
    uint32_t elapsed = uint32_t(millis()-start);
    out.remain=out.active && duration>elapsed ? (duration-elapsed+999)/1000 : 0;
    return out;
}
bool isCapturing() { CaptureStatus s=getCaptureStatus(); return s.active || s.pending; }
bool isCapturingToUsb() { CaptureStatus s=getCaptureStatus(); return (s.active || s.pending) && !s.toSd; }
void captureTick() {
    if(restorePending) {
        if(watchersTasksStopped()) {
            restoreDetector();
            portENTER_CRITICAL(&mux); restorePending=false; portEXIT_CRITICAL(&mux);
        } else stopWatchersWatch(); // retry if detector's mutex was busy at stop
    }
    portENTER_CRITICAL(&mux);
    bool pending=status.pending; uint32_t secs=requestedSecs; bool toSd=requestedSd;
    starting=pending;
    status.pending=false;
    portEXIT_CRITICAL(&mux);
    if(pending) {
        begin(secs,toSd);
        portENTER_CRITICAL(&mux); starting=false; portEXIT_CRITICAL(&mux);
    }
    if(!status.active) return;
    portENTER_CRITICAL(&mux); bool stopped=stopWanted; portEXIT_CRITICAL(&mux);
    uint32_t now=millis();
    if(stopped || status.error || capture_buffer::elapsed(now,startedAt,durationMs)) { finish(); return; }
    if(uint32_t(now-hopAt)>=250) {
        hopAt=now; channel=channel%11+1;
        if(esp_wifi_set_channel(channel,WIFI_SECOND_CHAN_NONE)!=ESP_OK) fail("channel hop failed");
    }
    drain(16);
    if(uint32_t(now-statAt)>=1000) {
        statAt=now;
        if(status.toSd) { flush(); if(!status.error && !sdStreamSync()) fail("card sync failed"); }
        if(!status.error) report(false);
    }
    if(status.error) finish();
}
#endif
