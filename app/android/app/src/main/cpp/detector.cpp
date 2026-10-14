// SPDX-License-Identifier: GPL-3.0-or-later
#include "detector_compat.h"
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 0
#include <ArduinoJson.h>
#include "detector/types.inc"
#include "attack_detectors.h"
#include "opendroneid.h"
#include "odid_wifi.h"
#include "vendor/miniz_tinfl.h"
#define PWN_HAS_GUNZIP 1
static bool watchersRunning=true, attackDetect=false, odidHeard=false;
static String huntMac;
static int huntWifiRssi=0,huntLastRssi=0,huntChannel=0;
static uint32_t huntWifiRssiMs=0,huntLastRssiMs=0,nativeNow=0;
static uint32_t millis(){return nativeNow;}
static void updateGeigerRssi(int){}
static std::vector<WatcherSignature> loadedSignatures;
static std::vector<WatcherTargetInfo> trackedTargets;
static std::vector<String> dirtyTargets;
#include "detector/weights.inc"
#define HACKING_GEAR_CATEGORY "Hacking gear"
static bool ruleGatedOff(const WatcherSignature& s){return !attackDetect&&s.category==HACKING_GEAR_CATEGORY;}
static void noteAlertForTarget(WatcherTargetInfo& t,int weight,const String&) {
    // UI owns sound/mute/ignore. Preserve strongest-signal eligibility and flood gating.
    if(weight>=CONF_ALERT_MIN&&!t.alerted){t.alerted=true;t.logPending=true;}
}
static WatcherTargetInfo* appendTrackedTarget(const WatcherTargetInfo& t){
    if(trackedTargets.size()>=256){auto it=std::max_element(trackedTargets.begin(),trackedTargets.end(),[](auto& a,auto& b){return (uint32_t)(nativeNow-a.lastSeenMs)<(uint32_t)(nativeNow-b.lastSeenMs);});trackedTargets.erase(it);}
    trackedTargets.push_back(t);return &trackedTargets.back();
}
#include "detector/ble.inc"
#include "detector/wifi.inc"
static void prune(){trackedTargets.erase(std::remove_if(trackedTargets.begin(),trackedTargets.end(),[](auto& t){return (uint32_t)(nativeNow-t.lastSeenMs)>WATCHERS_STALE_MS;}),trackedTargets.end());}
static void resetCore(const char* rules){
    trackedTargets.clear();loadedSignatures.clear();
    memset(&bleSpamState,0,sizeof(bleSpamState));memset(tagFloodState,0,sizeof(tagFloodState));memset(&deauthState,0,sizeof(deauthState));memset(&karmaState,0,sizeof(karmaState));
    JsonDocument d;if(deserializeJson(d,rules))throw std::runtime_error("Invalid signatures");
    for(JsonObject r:d.as<JsonArray>()){WatcherSignature s;s.name=r["name"]|"";s.category=r["category"]|"";s.oui=r["oui"]|"";s.mfgId=r["mfg_id"]|"";s.deviceName=r["device_name"]|"";s.serviceUuid=r["service_uuid"]|"";s.ssidPrefix=r["ssid_prefix"]|"";s.weight=r["weight"]|0;loadedSignatures.push_back(s);}
}
static std::map<String,uint32_t> previousCounts;
static bool changesOnly=false;
static std::string targetsJson(){
    JsonDocument d;JsonArray arr=d.to<JsonArray>();
    for(auto& t:trackedTargets){if(changesOnly?previousCounts[t.mac]==t.count:t.lastSeenMs!=nativeNow)continue;auto o=arr.add<JsonObject>();
        o["mac"]=t.mac.c_str();o["name"]=t.name.c_str();o["ssid"]=t.ssid.c_str();o["type"]=t.type.c_str();o["matched_rule"]=t.matchedRule.c_str();o["protocol"]=t.protocol.c_str();o["confidence"]=t.confidence;o["tier"]=t.tier.c_str();o["rssi"]=t.rssi;o["count"]=t.count;o["ch"]=t.wifiCh;o["pub"]=t.blePublic;o["cid"]=t.bleCompany;o["ring"]=t.bleRing;o["alert"]=t.logPending;t.logPending=false;
        if(t.wifiRole)o["ap"]=t.wifiRole==2?1:0;
        if(t.hasDrone){if(!t.uasId.empty())o["uas_id"]=t.uasId.c_str();if(!t.operatorId.empty())o["operator_id"]=t.operatorId.c_str();if(!t.selfId.empty())o["self_id"]=t.selfId.c_str();if(t.droneLat||t.droneLng){o["lat"]=t.droneLat;o["lng"]=t.droneLng;}if(t.altMsl>-1000)o["alt"]=t.altMsl;if(t.heightAgl>-1000)o["agl"]=t.heightAgl;if(t.speed>0)o["speed"]=t.speed;if(t.heading<361)o["heading"]=t.heading;if(t.opLat||t.opLng){o["op_lat"]=t.opLat;o["op_lng"]=t.opLng;}}
    }
    std::string out;serializeJson(d,out);return out;
}
#ifdef __ANDROID__
#include <jni.h>
#include <mutex>
static std::mutex coreMutex;
extern "C" JNIEXPORT void JNICALL Java_com_signalsweep_app_NativeDetector_resetRaw(JNIEnv* e,jclass,jbyteArray rules){std::lock_guard<std::mutex> lock(coreMutex);jsize n=e->GetArrayLength(rules);std::string p(n,'\0');e->GetByteArrayRegion(rules,0,n,(jbyte*)p.data());try{resetCore(p.c_str());}catch(const std::exception& x){e->ThrowNew(e->FindClass("java/lang/IllegalArgumentException"),x.what());}}
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_signalsweep_app_NativeDetector_feedRaw(JNIEnv* e,jclass,jboolean wifi,jstring mac,jint rssi,jint channel,jbyteArray bytes,jint addressType,jlong now,jboolean attack){
    std::lock_guard<std::mutex> lock(coreMutex);nativeNow=(uint32_t)now;attackDetect=attack;prune();
    previousCounts.clear();for(auto& t:trackedTargets)previousCounts[t.mac]=t.count;changesOnly=true;
    jsize n=e->GetArrayLength(bytes);if(n<0||n>65535)return e->NewByteArray(0);
    if(wifi){wifi_promiscuous_pkt_t p;p.rx_ctrl.sig_len=n+4;p.rx_ctrl.rssi=rssi;p.rx_ctrl.channel=channel;e->GetByteArrayRegion(bytes,0,n,(jbyte*)p.payload);memset(p.payload+n,0,4);watchersWifiPromiscuousCallback(&p,WIFI_PKT_MGMT);}
    else {NimBLEAdvertisedDevice d;auto m=e->GetStringUTFChars(mac,nullptr);d.addr={String(m),addressType};e->ReleaseStringUTFChars(mac,m);d.rssi=rssi;d.raw.resize(n);e->GetByteArrayRegion(bytes,0,n,(jbyte*)d.raw.data());d.parse();watchersScanCallbacks.dispatch(&d);}
    auto out=targetsJson();auto result=e->NewByteArray(out.size());e->SetByteArrayRegion(result,0,out.size(),(const jbyte*)out.data());return result;
}
#endif
