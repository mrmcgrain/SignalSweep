"""Deterministic positive/negative fixtures. No live device identifiers or packets."""
import json, gzip, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
rules=json.loads((root/'app/public/native-rules.json').read_text())
tests=[]
def ad(t,p): return bytes([len(p)+1,t])+p
def ie(t,p): return bytes([t,len(p)])+p
def uuid(u):
    u=u.lower().removeprefix('0x')
    return ad(3,int(u,16).to_bytes(2,'little')) if len(u)==4 else ad(7,bytes.fromhex(u.replace('-',''))[::-1])
def wifi(mac,sub=8,ies=b'',dest='ff:ff:ff:ff:ff:ff'):
    m=bytes.fromhex(mac.replace(':',''));d=bytes.fromhex(dest.replace(':',''))
    return bytes([sub<<4,0,0,0])+d+m+m+b'\0\0'+(b'\0'*12 if sub in (5,8) else b'')+ies
def step(raw,radio='BLE',mac='00:11:22:33:44:55',**kw): return dict(hex=raw.hex(),radio=radio,mac=mac,rssi=-45,ch=6,at=kw.pop('at',1000),**kw)
def case(name,steps,expect,ruleset=None):tests.append(dict(id=name,rules=ruleset if ruleset is not None else [],steps=steps,expect=expect))
for i,r in enumerate(rules):
    mac=((r.get('oui') or '00:11:22')+':33:44:55').upper()
    raw=b'';score=0
    if r.get('mfg_id'):raw+=ad(255,int(r['mfg_id'],0).to_bytes(2,'little')+b'\0');score+=45
    if r.get('device_name'):raw+=ad(9,r['device_name'].encode());score+=70
    if r.get('service_uuid'):raw+=uuid(r['service_uuid']);score+=70
    if r.get('oui'):score+=30
    radio='BLE'
    if r.get('ssid_prefix'):
        radio='WiFi';raw=wifi(mac,ies=ie(0,r['ssid_prefix'].encode()));score=(r.get('weight') or 80)+(r.get('weight') or 30 if r.get('oui') else 0)
    else:score=r.get('weight') or score
    case('generic-%02d'%i,[step(raw,radio,mac)],{'type':r['category'],'confidence':min(100,score)},[r])
    # Remove every declared BLE condition in turn: rules AND their conditions.
    if radio=='BLE':
        parts=[]
        if r.get('mfg_id'):parts.append(ad(255,int(r['mfg_id'],0).to_bytes(2,'little')+b'\0'))
        if r.get('device_name'):parts.append(ad(9,r['device_name'].encode()))
        if r.get('service_uuid'):parts.append(uuid(r['service_uuid']))
        for j in range(len(parts)):case('generic-%02d-missing-%d'%(i,j),[step(b''.join(parts[:j]+parts[j+1:]),mac=mac)],{'confidence':0},[r])
    if r['category']=='Hacking gear':
        # Positive rule is tested with Attack on, plus a separate gated negative.
        for t in tests:
            if t['id'].startswith('generic-%02d'%i):t['steps'][0]['attack']=True
        case('generic-%02d-attack-off'%i,[step(raw,radio,mac)],{'confidence':0},[r])
case('airtag-long',[step(ad(255,bytes.fromhex('4c001219')+bytes(25)))],{'type':'Tracker','confidence':80,'matched_rule':'Apple Find My Tracker'})
case('apple-short-not-tracker',[step(ad(255,bytes.fromhex('4c0012020002')))],{'type':'','confidence':0})
for u,name in [(0xfeed,'Tile Tracker'),(0xfd5a,'Samsung SmartTag'),(0xfd59,'Samsung SmartTag (setup)')]:case(name,[step(ad(0x16,u.to_bytes(2,'little')+b'\1'))],{'type':'Tracker','matched_rule':name,'confidence':80})
for byte,name in [(0x40,'Google Find My Tracker'),(0x41,'Google Find My Tracker (unwanted-tracking mode)')]:case(name,[step(ad(0x16,bytes.fromhex('aafe')+bytes([byte])+bytes(20)))],{'matched_rule':name,'confidence':80})
case('eddystone-not-tracker',[step(ad(0x16,bytes.fromhex('aafe00')+bytes(20)))],{'type':'','confidence':0})
case('penguin-legacy',[step(ad(9,b'Penguin-1234567890'))],{'type':'Flock Safety','confidence':80})
case('penguin-modern',[step(ad(9,b'1234567890')+ad(255,bytes.fromhex('c809')+b'TN123'))],{'type':'Flock Safety','confidence':80})
case('serial-alone-not-penguin',[step(ad(9,b'0102000000'))],{'type':'','confidence':0})
basic=bytes.fromhex('0212')+b'TEST-DRONE-123'.ljust(20,b'\0')+bytes(3)
rid=ad(0x16,bytes.fromhex('faff0d01')+basic)
case('ble-remote-id-basic',[step(rid)],{'type':'Drone','confidence':90,'uas_id':'TEST-DRONE-123'})
case('wifi-remote-id-basic',[step(wifi('00:11:22:33:44:55',ies=ie(221,bytes.fromhex('903ae60d01')+basic)),'WiFi')],{'type':'Drone','confidence':90,'uas_id':'TEST-DRONE-123'})
case('remote-id-short-presence',[step(ad(0x16,bytes.fromhex('faff0d01')))],{'type':'Drone','confidence':90})
oui=[{'name':'Flock test','category':'Flock Safety','oui':'B8:27:EB'}]
probe=wifi('b8:27:eb:33:44:55',4,ie(0,b'')+ie(221,bytes.fromhex('506f9a16030103')))
case('flock-probe-ie',[step(probe,'WiFi','B8:27:EB:33:44:55')],{'type':'Flock Safety','confidence':100,'matched_rule':'Flock probe + IE'},oui)
case('consumer-ie-not-flock',[step(wifi('00:11:22:33:44:55',4,ie(0,b'')+ie(221,bytes.fromhex('506f9a16030103'))),'WiFi')],{'type':'','confidence':30},oui)
case('flock-ssid',[step(wifi('00:11:22:33:44:55',ies=ie(0,b'Flock-123456')),'WiFi')],{'type':'Flock Safety','confidence':80})
case('flocknation-negative',[step(wifi('00:11:22:33:44:55',ies=ie(0,b'FlockNation')),'WiFi')],{'type':'','confidence':0})
for compressed in (False,True):
    data=json.dumps({'name':'fixture','pwnd_tot':7,'grid_version':'1'}).encode()
    if compressed:data=gzip.compress(data,mtime=0)
    ies=ie(222,data)+(ie(223,b'\1') if compressed else b'')
    case('pwngrid-'+('gzip' if compressed else 'json'),[step(wifi('de:ad:be:ef:de:ad',ies=ies),'WiFi',mac='DE:AD:BE:EF:DE:AD',attack=True)],{'type':'Hacking gear','confidence':100})
case('deauth-burst',[step(wifi('00:11:22:33:44:55',12,b'\1\0'),'WiFi',at=1000+i,attack=True) for i in range(15)],{'type':'Hacking gear','confidence':90})
case('deauth-gated',[step(wifi('00:11:22:33:44:55',12,b'\1\0'),'WiFi',at=1000+i) for i in range(15)],{'absent':True})
case('karma',[step(wifi('00:11:22:33:44:55',5,ie(0,('test%d'%i).encode())),'WiFi',at=1000+i,attack=True) for i in range(4)],{'type':'Hacking gear','confidence':90})
case('ble-popup-spam',[step(ad(255,bytes.fromhex('4c0007')),mac='C0:00:00:00:00:%02X'%i,address_type=1,at=1000+i,attack=True) for i in range(8)],{'mac':'BLE popup spam','type':'Hacking gear','confidence':90})
case('public-apple-no-spam',[step(ad(255,bytes.fromhex('4c0007')),mac='00:00:00:00:00:%02X'%i,address_type=0,at=1000+i,attack=True) for i in range(8)],{'type':'','confidence':0})
case('truncated-ad',[step(bytes.fromhex('ff16faff'))],{'type':'','confidence':0})
case('truncated-ie',[step(wifi('00:11:22:33:44:55',ies=bytes.fromhex('ddff903ae6')),'WiFi')],{'type':'','confidence':0})
case('tag-flood',[step(ad(0x16,bytes.fromhex('edfe01')),mac='C0:00:00:00:00:%02X'%i,at=1000+i) for i in range(6)]+[step(ad(0x16,bytes.fromhex('edfe01')),mac='C0:00:00:00:00:20',at=32000)],{'mac':'Tag flood: Tile','type':'Tracker','confidence':90})
case('split-ad-response-sticky',[step(ad(255,bytes.fromhex('c809')+b'TN123')),step(ad(9,b'ordinary'),at=1100)],{'type':'Flock Safety','confidence':80,'cid':2504})
case('cross-radio-corroboration',[step(ad(0x16,bytes.fromhex('edfe01'))),step(wifi('00:11:22:33:44:55',ies=ie(0,b'normal')),'WiFi',at=1100)],{'type':'Tracker','confidence':80,'protocol':'BLE+WiFi'})
case('millis-wrap',[step(wifi('00:11:22:33:44:55',12,b'\1\0'),'WiFi',at=(0xfffffff8+i)&0xffffffff,attack=True) for i in range(15)],{'type':'Hacking gear','confidence':90})
if True:
    enc=json.loads(Path(sys.argv[1] if len(sys.argv)>1 else root/'app/tools/odid-fixture-payloads.json').read_text());msgs=[basic]+[bytes.fromhex(enc[k]) for k in ('location','system','operator','self')]
    expect={'type':'Drone','confidence':90,'uas_id':'TEST-DRONE-123','operator_id':'TEST-OPERATOR','self_id':'Test fixture','lat':33.5,'lng':-112,'alt':100,'agl':20,'heading':90,'speed':5,'op_lat':33.4,'op_lng':-111.9}
    case('ble-remote-id-stream',[step(ad(0x16,bytes.fromhex('faff0d01')+m),at=1000+i) for i,m in enumerate(msgs)],expect)
    case('ble-remote-id-packed',[step(ad(0x16,bytes.fromhex('faff0d01')+bytes.fromhex(enc['pack'])))],expect)
    case('wifi-remote-id-packed',[step(wifi('00:11:22:33:44:55',ies=ie(221,bytes.fromhex('903ae60d01')+bytes.fromhex(enc['pack']))),'WiFi')],expect)
    case('wifi-remote-id-nan',[step(bytes.fromhex(enc['nan']),'WiFi')],expect)
case('unknown-apple-no-spam',[step(ad(255,bytes.fromhex('4c0007')),mac='00:00:00:00:00:%02X'%i,address_type=-1,at=1000+i,attack=True) for i in range(8)],{'type':'','confidence':0})
case('unicode-name',[step(ad(9,'Fixture 🎯'.encode()))],{'name':'Fixture 🎯','confidence':70},[{'name':'unicode rule','category':'Tracker','device_name':'Fixture 🎯'}])
case('service-data-not-uuid-list',[step(ad(0x16,bytes.fromhex('0df001')))],{'confidence':0},[{'name':'UUID rule','category':'Tracker','service_uuid':'f00d'}])
case('tile-data-single-evidence',[step(ad(0x16,bytes.fromhex('edfe01')))],{'type':'Tracker','confidence':80},[{'name':'UUID rule','category':'Tracker','service_uuid':'feed'}])
case('complete-name-preferred',[step(ad(9,b'Complete name')+ad(8,b'Short'))],{'name':'Complete name','confidence':0})
out=root/'app/android/app/src/main/assets/decoder-fixtures.json'
out.write_text(json.dumps(tests,indent=2)+'\n')
print(len(tests),'decoder fixtures written')
