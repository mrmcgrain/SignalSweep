// Execute the actual classic app script against a fake Android bridge.
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const noop=()=>{};
const elements=new Map();
function element(id) {
    if(!elements.has(id)) elements.set(id,{id,style:{},dataset:{},classList:{add:noop,remove:noop,toggle:noop,contains:()=>false},
        setAttribute:noop,querySelector:()=>null,querySelectorAll:()=>[],textContent:'',innerHTML:'',hidden:false});
    return elements.get(id);
}
const listeners=new Map();
let stops=0;
const rules=JSON.parse(readFileSync(new URL('./public/native-rules.json',import.meta.url)));
const ctx={console,TextEncoder,TextDecoder,Date,Map,Set,Uint8Array,DataView,Promise,ArrayBuffer,
    setTimeout:()=>0,clearTimeout:noop,setInterval:()=>0,clearInterval:noop,atob,btoa,
    localStorage:{getItem:()=>null,setItem:noop,removeItem:noop}, navigator:{},
    fetch:async()=>({ok:true,json:async()=>rules}),
    document:{getElementById:element,addEventListener:noop,querySelector:()=>null,querySelectorAll:()=>[],visibilityState:'visible'},
    Capacitor:{getPlatform:()=> 'android',isNativePlatform:()=>true,isPluginAvailable:()=>true},
    PhoneScanner:{stop:async()=>{stops++},start:async()=>({session:12}),
        authorizeRoot:async()=>{},configure:async()=>{},captureStart:async()=>({recording:true}),captureStop:async()=>{},
        addListener:async(name,cb)=>{listeners.set(name,cb);return {remove:async()=>listeners.delete(name)}}},
};
ctx.window=ctx;ctx.globalThis=ctx;
vm.createContext(ctx);
vm.runInContext(readFileSync(new URL('./public/scan-sources.js',import.meta.url),'utf8'),ctx);
vm.runInContext(readFileSync(new URL('./public/app.js',import.meta.url),'utf8'),ctx);
await vm.runInContext('startPhoneScan()',ctx);
assert.equal(vm.runInContext('connectionType',ctx),'PHONE');
assert.equal(vm.runInContext('sourceCapabilities.capture',ctx),false);
listeners.get('observations')({session:11,targets:[{mac:'OLD',observed_at:Date.now(),protocol:'BLE'}]});
assert.equal(vm.runInContext('Object.keys(liveMatches).length',ctx),0);
listeners.get('observations')({session:12,targets:[{mac:'AA:BB:CC:DD:EE:FF',observed_at:Date.now(),protocol:'BLE',rssi:-55,name:'Tile',type:'Tracker',confidence:80,decoded:true}]});
assert.equal(vm.runInContext('Object.keys(liveMatches).length',ctx),1);
assert.equal(vm.runInContext('liveMatches["AA:BB:CC:DD:EE:FF"].type',ctx),'Tracker');
await vm.runInContext('sendCommand({scan_all:false})',ctx);
assert.equal(vm.runInContext('phoneOptions.scan_all',ctx),false);
await vm.runInContext('disconnectDevice()',ctx);
assert.equal(vm.runInContext('connectionType',ctx),null);
assert.equal(vm.runInContext('Object.keys(liveMatches).length',ctx),0);
assert.equal(listeners.size,0); assert.ok(stops>0);
await vm.runInContext('startPhoneScan(true)',ctx);
assert.equal(vm.runInContext('phoneNetHunter',ctx),true);
assert.equal(vm.runInContext('sourceCapabilities.capture',ctx),true);
assert.equal(vm.runInContext('sourceCapabilities.backend',ctx),'nethunter-pcap');
await vm.runInContext('startCapture()',ctx);
assert.equal(vm.runInContext('capturing',ctx),true);
listeners.get('captureSaved')({path:'/capture.pcap',ble_path:'/capture-ble.jsonl',wifi:20,ble:30});
assert.equal(vm.runInContext('capturing',ctx),false);
assert.equal(element('cap-path').textContent,'/capture.pcap\n/capture-ble.jsonl');
await vm.runInContext('disconnectDevice()',ctx);
// External source changes must close the old transport before opening another.
const handoff=[];
ctx.UsbSerial={stopReading:async()=>handoff.push('stopUSB'),close:async()=>handoff.push('closeUSB'),write:async()=>{}};
ctx.BleClient={getConnectedDevices:async()=>[],disconnect:async()=>handoff.push('closeBLE')};
ctx.connectNativeBluetooth=async()=>{
    assert.equal(vm.runInContext('usbPortId',ctx),null);
    assert.equal(vm.runInContext('usbListeners.length',ctx),0);
    handoff.push('openBLE');
};
vm.runInContext("connectionType='USB';usbPortId='old';usbListeners=[{remove:async()=>{}}]",ctx);
await vm.runInContext('connectWebBluetooth()',ctx);
assert.deepEqual(handoff,['stopUSB','closeUSB','openBLE']);
handoff.length=0;
ctx.connectNativeUsb=async()=>{handoff.push('openUSB');};
vm.runInContext("connectionType='BLE';bleDevice={deviceId:'old-board'}",ctx);
await vm.runInContext('connectUsb()',ctx);
assert.deepEqual(handoff,['closeBLE','openUSB']);
console.log('Actual app phone source start, stale callback rejection, filter and stop cleanup passed');
