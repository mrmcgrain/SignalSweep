// SPDX-License-Identifier: GPL-3.0-or-later
// Android facade for the Arduino/NimBLE types used by the shared callbacks.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
class String : public std::string {
public:
    using std::string::string;
    String() = default;
    String(const std::string& s):std::string(s) {}
    bool startsWith(const String& s) const { return compare(0,s.size(),s)==0; }
    bool equalsIgnoreCase(const String& s) const { return strcasecmp(c_str(),s.c_str())==0; }
    int indexOf(const String& s) const { auto p=find(s); return p==npos?-1:(int)p; }
    void toLowerCase() { for(char& c:*this)c=(char)tolower((unsigned char)c); }
    void toUpperCase() { for(char& c:*this)c=(char)toupper((unsigned char)c); }
    void trim() { auto a=find_first_not_of(" \r\n\t"),b=find_last_not_of(" \r\n\t"); *this=a==npos?String():substr(a,b-a+1); }
    String substring(size_t a) const { return substr(a); }
};
class NimBLEUUID {
    String value;
    static String normalize(String s) { s.toLowerCase(); if(s.size()==4) return "0000"+s+"-0000-1000-8000-00805f9b34fb"; return s; }
public:
    NimBLEUUID(uint16_t n) { char b[5];snprintf(b,sizeof(b),"%04x",n);value=b; }
    NimBLEUUID(String s):value(s) {}
    int bitSize() const { return value.size()==4?16:value.size()==8?32:128; }
    std::string toString() const { return value; }
    bool operator==(const NimBLEUUID& other) const { return normalize(value)==normalize(other.value); }
};
struct NativeAddress { String mac; int type; std::string toString()const{return mac;} int getType()const{return type;} };
#define BLE_ADDR_PUBLIC 0
#define BLE_ADDR_RANDOM 1
class NimBLEAdvertisedDevice {
public:
    NativeAddress addr;
    int rssi=0;
    String name;
    std::string manufacturer;
    std::vector<uint8_t> raw;
    std::vector<NimBLEUUID> services;
    std::map<uint16_t,std::string> serviceData;
    NativeAddress getAddress()const{return addr;}
    int getRSSI()const{return rssi;}
    bool haveName()const{return !name.empty();}
    std::string getName()const{return name;}
    bool haveManufacturerData()const{return !manufacturer.empty();}
    std::string getManufacturerData()const{return manufacturer;}
    bool haveServiceUUID()const{return !services.empty();}
    size_t getServiceUUIDCount()const{return services.size();}
    NimBLEUUID getServiceUUID(size_t i)const{return services.at(i);}
    size_t getServiceDataCount()const{return serviceData.size();}
    std::string getServiceData(NimBLEUUID u)const { for(auto& p:serviceData)if(u==NimBLEUUID(p.first))return p.second;return {}; }
    bool isAdvertisingService(NimBLEUUID u)const{for(auto& s:services)if(s==u)return true;return false;}
    uint8_t* getPayload(){return raw.data();}
    size_t getPayloadLength()const{return raw.size();}
    void parse() {
        bool completeName=false;
        for(size_t i=0;i+1<raw.size();) {
            size_t n=raw[i]; if(!n||i+1+n>raw.size())break;
            uint8_t t=raw[i+1];auto p=raw.data()+i+2;size_t len=n-1;
            if(t==9&&!completeName){name=String((const char*)p,len);completeName=true;}
            else if(t==8&&!completeName&&name.empty())name=String((const char*)p,len);
            if(t==255 && manufacturer.empty())manufacturer.assign((const char*)p,len);
            if(t==2||t==3)for(size_t j=0;j+1<len;j+=2)services.emplace_back((uint16_t)(p[j]|p[j+1]<<8));
            if(t==4||t==5)for(size_t j=0;j+3<len;j+=4){char b[9];snprintf(b,sizeof(b),"%02x%02x%02x%02x",p[j+3],p[j+2],p[j+1],p[j]);services.emplace_back(String(b));}
            if(t==6||t==7)for(size_t j=0;j+15<len;j+=16){char b[37];snprintf(b,sizeof(b),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",p[j+15],p[j+14],p[j+13],p[j+12],p[j+11],p[j+10],p[j+9],p[j+8],p[j+7],p[j+6],p[j+5],p[j+4],p[j+3],p[j+2],p[j+1],p[j]);services.emplace_back(String(b));}
            if(t==0x16 && len>=2){uint16_t u=p[0]|p[1]<<8;serviceData.emplace(u,std::string((const char*)p+2,len-2));}
            i+=n+1;
        }
    }
};
class NimBLEAdvertisedDeviceCallbacks {
public:
    virtual void onResult(NimBLEAdvertisedDevice*)=0;
    void dispatch(NimBLEAdvertisedDevice* d){onResult(d);}
};
using SWEEP_ADV=NimBLEAdvertisedDevice;
struct wifi_promiscuous_pkt_t { struct { int sig_len=0,rssi=0,channel=0,rx_state=0; }rx_ctrl; uint8_t payload[65540]; };
using wifi_promiscuous_pkt_type_t=int;
#define WIFI_PKT_MGMT 0
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
static void* watchersMutex=(void*)1;
static int xSemaphoreTake(void*,int){return 1;}
static void xSemaphoreGive(void*){}
#define ESP_LOGI(...) ((void)0)
