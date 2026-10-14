// SPDX-License-Identifier: GPL-3.0-or-later
#include "opendroneid.h"
#include "odid_wifi.h"
#include <stdio.h>
static void dump(const char* key,const uint8_t* b,int n){printf("\"%s\":\"",key);for(int i=0;i<n;i++)printf("%02x",b[i]);printf("\"");}
int main(void){
    ODID_UAS_Data u;odid_initUasData(&u);
    u.BasicIDValid[0]=u.LocationValid=u.SystemValid=u.OperatorIDValid=u.SelfIDValid=1;
    u.BasicID[0].IDType=ODID_IDTYPE_SERIAL_NUMBER;u.BasicID[0].UAType=ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;strcpy(u.BasicID[0].UASID,"TEST-DRONE-123");
    u.Location.Latitude=33.5;u.Location.Longitude=-112.0;u.Location.AltitudeGeo=100;u.Location.Height=20;u.Location.Direction=90;u.Location.SpeedHorizontal=5;
    u.System.OperatorLatitude=33.4;u.System.OperatorLongitude=-111.9;
    strcpy(u.OperatorID.OperatorId,"TEST-OPERATOR");strcpy(u.SelfID.Desc,"Test fixture");
    ODID_Location_encoded l;ODID_System_encoded s;ODID_OperatorID_encoded o;ODID_SelfID_encoded self;
    if(encodeLocationMessage(&l,&u.Location)||encodeSystemMessage(&s,&u.System)||encodeOperatorIDMessage(&o,&u.OperatorID)||encodeSelfIDMessage(&self,&u.SelfID))return 1;
    uint8_t pack[1024],nan[1024];char mac[6]={0,0x11,0x22,0x33,0x44,0x55};
    int pn=odid_message_build_pack(&u,pack,sizeof(pack));int nn=odid_wifi_build_message_pack_nan_action_frame(&u,mac,1,nan,sizeof(nan));if(pn<0||nn<0)return 1;
    printf("{");dump("location",(uint8_t*)&l,25);printf(",");dump("system",(uint8_t*)&s,25);printf(",");dump("operator",(uint8_t*)&o,25);printf(",");dump("self",(uint8_t*)&self,25);printf(",");dump("pack",pack,pn);printf(",");dump("nan",nan,nn);printf("}\n");return 0;
}
