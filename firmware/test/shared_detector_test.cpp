// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../app/android/app/src/main/cpp/detector.cpp"
#include <fstream>
#include <iostream>
static std::vector<uint8_t> unhex(const char* h){std::vector<uint8_t> b;for(size_t i=0;h[i]&&h[i+1];i+=2){char s[]={h[i],h[i+1],0};b.push_back((uint8_t)strtoul(s,nullptr,16));}return b;}
int main(int argc,char** argv){
    if(argc!=2)return 2;std::ifstream in(argv[1]);JsonDocument cases;if(deserializeJson(cases,in))return 2;
    int total=0;
    for(JsonObject c:cases.as<JsonArray>()){
        std::string rules;serializeJson(c["rules"],rules);resetCore(rules.c_str());
        for(JsonObject s:c["steps"].as<JsonArray>()){
            nativeNow=s["at"].as<uint32_t>();attackDetect=s["attack"]|false;prune();auto raw=unhex(s["hex"]|"");
            if(strcmp(s["radio"]|"BLE","WiFi")==0){wifi_promiscuous_pkt_t p{};p.rx_ctrl.sig_len=raw.size()+4;p.rx_ctrl.rssi=s["rssi"]|-45;p.rx_ctrl.channel=s["ch"]|6;memcpy(p.payload,raw.data(),raw.size());watchersWifiPromiscuousCallback(&p,0);}
            else{NimBLEAdvertisedDevice d;d.addr={String(s["mac"]|"00:11:22:33:44:55"),s["address_type"]|0};d.rssi=s["rssi"]|-45;d.raw=raw;d.parse();watchersScanCallbacks.dispatch(&d);}
        }
        JsonDocument result;deserializeJson(result,targetsJson());JsonObject expected=c["expect"];JsonObject selected;
        for(JsonObject r:result.as<JsonArray>()){if(!expected["mac"].isNull()){if(strcmp(r["mac"]|"",expected["mac"]|"")==0)selected=r;}else if(selected.isNull()||(r["confidence"]|0)>(selected["confidence"]|0))selected=r;}
        bool ok=expected["absent"]|false?selected.isNull():!selected.isNull();
        for(JsonPair e:expected){std::string key=e.key().c_str();if(key=="absent")continue;std::string a,b;serializeJson(e.value(),a);serializeJson(selected[key],b);if(a!=b){ok=false;std::cerr<<c["id"].as<const char*>()<<": "<<key<<" expected "<<a<<" got "<<b<<"\n";}}
        if(!ok)return 1;total++;
    }
    std::cout<<"PASS "<<total<<" shared callback fixtures\n";return 0;
}
