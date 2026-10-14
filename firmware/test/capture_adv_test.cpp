#include "capture_stubs/platform.h"
#include "../src/mode_capture.h"
#include "../src/capture_buffer.h"
#include <cassert>
#include <fstream>

void packet(unsigned count=1) {
    if(!wifiCb || !isCapturing()) fprintf(stderr,"packet outside capture after %u starts: %s\n",stopCount,getCaptureStatus().error?getCaptureStatus().error:"no error");
    assert(wifiCb && isCapturing());
    wifi_promiscuous_pkt_t p; p.rx_ctrl.sig_len=300; p.payload[0]=0x80;
    for(unsigned i=0;i<count;++i) wifiCb(&p,WIFI_PKT_MGMT);
}
void advert() { assert(fakeScan.cb); NimBLEAdvertisedDevice d; fakeScan.cb->onResult(&d); }
int main(int argc,char** argv) {
    capture_buffer::Queue<2> queue;
    uint8_t data[300]; memset(data,0x55,sizeof(data));
    assert(queue.push(0,0x12345678,6,-80,data,300,300));
    assert(queue.push(0,0,1,-1,data,3,3));
    assert(!queue.push(0,0,1,-1,data,3,3));
    capture_buffer::Record r;
    assert(queue.pop(r) && r.length==256 && r.original==300);
    uint8_t wire[271]; assert(capture_buffer::encode(r,wire)==271);
    assert(wire[5]==0x78 && wire[6]==0x56 && wire[10]==176 && wire[11]==44 && wire[12]==1 && wire[14]==1);
    assert(queue.push(1,0,0,-60,data,3,3));
    assert(queue.pop(r) && r.seq==1); assert(queue.pop(r) && r.seq==3);
    assert(queue.dropped==1 && !queue.pop(r));
    assert(!capture_buffer::elapsed(5,0xfffffff0,30));
    assert(capture_buffer::elapsed(20,0xfffffff0,30));

    requestCapture(60,true); assert(isCapturing()); stopCapture(); captureTick();
    assert(!isCapturing() && stopCount==0); // cancel before start
    cardState=SD_NONE; requestCapture(60,true); captureTick();
    assert(!isCapturing() && stopCount==0 && getCaptureStatus().error);
    cardState=SD_OK; requestCapture(1,true); requestCapture(1,true);
    assert(usbReplies.back().find("busy")!=std::string::npos);
    captureTick(); assert(isCapturing() && cardOpen && !fakeScan.activeScan);
    packet(40); advert();
    assert(getCaptureStatus().wifi==32 && getCaptureStatus().drops==8);
    captureTick(); packet(); // queue reuse after overflow
    clockMs+=1001; captureTick();
    assert(!isCapturing() && !cardOpen && restartCount==1 && getCaptureStatus().written==34);
    assert(cardData.find("#SSCAP END wifi=33 ble=1 drops=8 written=34")!=std::string::npos);
    if(argc>1) { std::ofstream out(argv[1]); out<<cardData; }

    bleEnabled=false; wifiEnabled=false;
    requestCapture(0,false); captureTick(); packet(); advert(); captureTick();
    assert(usbRecords.size()==2 && cardData.find("#SSCAP END")!=std::string::npos);
    stopCapture(); captureTick();
    assert(!isCapturing() && blePaused && wifiPaused && restartCount==2);

    requestCapture(0,true); captureTick(); packet(); writeFail=true;
    clockMs+=1001; captureTick(); writeFail=false;
    assert(!isCapturing() && !cardOpen && restartCount==3);
    assert(usbReplies.back().find("done\":false")!=std::string::npos);
    cardState=SD_OK; requestCapture(0,true); captureTick(); syncFail=true;
    clockMs+=1001; captureTick(); syncFail=false;
    assert(!isCapturing() && !cardOpen && restartCount==4);
    assert(std::string(getCaptureStatus().error)=="card sync failed");

    radioFail=true; requestCapture(0,true); captureTick(); radioFail=false;
    assert(!isCapturing() && restartCount==5 && !cardOpen);
    tasksStopped=false; requestCapture(0,true); captureTick();
    assert(isCapturing() && !cardOpen && restartCount==5);
    tasksStopped=true; captureTick(); assert(!isCapturing() && restartCount==6);
    requestCapture(3601,true); assert(!isCapturing());
    assert(usbReplies.back().find("duration")!=std::string::npos);

    clockMs=0xfffffff0; requestCapture(1,false); captureTick();
    clockMs+=1001; captureTick(); assert(!isCapturing() && restartCount==7);
    puts("ADV capture lifecycle, SD failures, queue overflow, format and timer tests passed");
}
