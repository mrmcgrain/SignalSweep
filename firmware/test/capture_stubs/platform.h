// Host fakes for the actual ADV capture lifecycle, not radio hardware proof.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <Arduino.h>
inline uint32_t clockMs=0;
inline uint32_t millis() { return clockMs; }
inline uint32_t micros() { return clockMs*1000; }
inline void delay(unsigned ms) { clockMs+=ms; }
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
using esp_err_t=int;
constexpr int ESP_OK=0, WIFI_STA=1, WIFI_SECOND_CHAN_NONE=0;
constexpr uint32_t WIFI_PROMIS_FILTER_MASK_MGMT=1, WIFI_PROMIS_FILTER_MASK_DATA=2, WIFI_PROMIS_FILTER_MASK_CTRL=4;
enum wifi_promiscuous_pkt_type_t { WIFI_PKT_MGMT, WIFI_PKT_DATA, WIFI_PKT_CTRL, WIFI_PKT_MISC };
struct wifi_promiscuous_pkt_t {
    struct { unsigned sig_len=0; uint8_t channel=1, rx_state=0; int8_t rssi=-50; } rx_ctrl;
    uint8_t payload[1024]={};
};
struct wifi_promiscuous_filter_t { uint32_t filter_mask; };
inline void (*wifiCb)(void*,wifi_promiscuous_pkt_type_t)=nullptr;
inline bool promiscuous=false;
inline bool radioFail=false;
inline esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t*) { return radioFail?1:0; }
inline esp_err_t esp_wifi_set_channel(uint8_t,int) { return radioFail?1:0; }
inline esp_err_t esp_wifi_set_promiscuous_rx_cb(void (*cb)(void*,wifi_promiscuous_pkt_type_t)) { wifiCb=cb; return 0; }
inline esp_err_t esp_wifi_set_promiscuous(bool b) { promiscuous=b; return 0; }
struct FakeWiFi { void mode(int) {} void disconnect() {} };
inline FakeWiFi WiFi;
class NimBLEAddress {
    uint8_t mac[6]={6,5,4,3,2,1};
public:
    const uint8_t* getNative() const { return mac; }
    uint8_t getType() const { return 1; }
};
class NimBLEAdvertisedDevice {
public:
    NimBLEAddress getAddress() { return {}; }
    size_t getPayloadLength() { return 3; }
    const uint8_t* getPayload() { static uint8_t p[]={2,1,6}; return p; }
    int getRSSI() { return -60; }
};
class NimBLEAdvertisedDeviceCallbacks { public: virtual ~NimBLEAdvertisedDeviceCallbacks()=default; virtual void onResult(NimBLEAdvertisedDevice*) {} };
class NimBLEScan {
public:
    NimBLEAdvertisedDeviceCallbacks* cb=nullptr;
    bool activeScan=true, running=false;
    void stop() { running=false; }
    void clearResults() {}
    void setAdvertisedDeviceCallbacks(NimBLEAdvertisedDeviceCallbacks* c,bool) { cb=c; }
    void setActiveScan(bool b) { activeScan=b; }
    void setMaxResults(int) {} void setInterval(int) {} void setWindow(int) {}
    bool start(int,void*,bool) { running=true; return true; }
};
inline NimBLEScan fakeScan;
struct NimBLEDevice { static NimBLEScan* getScan() { return &fakeScan; } };
enum { SD_NONE, SD_OK, SD_ERROR, SD_FULL };
inline uint8_t cardState=SD_OK;
inline std::string cardData;
inline bool cardOpen=false, writeFail=false, syncFail=false, tasksStopped=true;
inline unsigned stopCount=0, restartCount=0, releaseCount=0;
inline bool bleEnabled=true, wifiEnabled=true, blePaused=false, wifiPaused=false;
inline uint8_t sdState() { return cardState; }
inline bool sdProbe() { return cardState==SD_OK; }
inline bool sdNextCaptureName(char* out) { strcpy(out,"CAP00001.SSC"); return true; }
inline bool sdStreamOpen(const char*) { cardOpen=true; cardData.clear(); return true; }
inline bool sdStreamWrite(const uint8_t* p,size_t n) { if(writeFail) { cardState=SD_ERROR; return false; } cardData.append((const char*)p,n); return true; }
inline bool sdStreamSync() { return !syncFail; }
inline void sdStreamClose() { cardOpen=false; }
inline void stopWatchersWatch() { ++stopCount; }
inline bool watchersTasksStopped() { return tasksStopped; }
inline void startWatchersWatch() { ++restartCount; }
inline void pauseBle(bool b) { blePaused=b; }
inline void pauseWifi(bool b) { wifiPaused=b; }
inline bool isBleScanEnabled() { return bleEnabled; }
inline bool isWifiScanEnabled() { return wifiEnabled; }
inline void cardputerUiPrepareCapture() { ++releaseCount; }
inline uint32_t hostTimeNow() { return 0; }
inline std::vector<String> usbReplies, bleReplies;
inline std::vector<std::string> usbRecords;
inline void sendUsbLine(const String& s) { usbReplies.push_back(s); }
inline void sendBleSerial(const String& s) { bleReplies.push_back(s); }
inline void sendUsbLine(const char*,const uint8_t* p,size_t n) { usbRecords.emplace_back((const char*)p,n); }
inline int mbedtls_base64_encode(uint8_t* out,size_t max,size_t* used,const uint8_t* in,size_t n) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *used=4*((n+2)/3); if(max<*used+1) return -1;
    size_t j=0;
    for(size_t i=0; i<n; i+=3) {
        uint32_t v=uint32_t(in[i])<<16;
        if(i+1<n) v|=uint32_t(in[i+1])<<8;
        if(i+2<n) v|=in[i+2];
        out[j++]=alphabet[(v>>18)&63]; out[j++]=alphabet[(v>>12)&63];
        out[j++]=i+1<n?alphabet[(v>>6)&63]:'=';
        out[j++]=i+2<n?alphabet[v&63]:'=';
    }
    out[j]=0; return 0;
}
