package com.signalsweep.app;

import android.Manifest;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.location.LocationManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Base64;
import com.getcapacitor.JSArray;
import com.getcapacitor.JSObject;
import com.getcapacitor.PermissionState;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.CapacitorPlugin;
import com.getcapacitor.annotation.Permission;
import com.getcapacitor.annotation.PermissionCallback;
import java.util.Collections;

/** Explicit foreground BLE advertisements and fresh Wi-Fi AP observations. */
@CapacitorPlugin(name = "PhoneScanner", permissions = {
    @Permission(alias = "location", strings = {Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION}),
    @Permission(alias = "bluetooth", strings = {Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT})
})
public class PhoneScannerPlugin extends Plugin {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private BluetoothLeScanner ble;
    private WifiManager wifi;
    private volatile boolean running = false;
    private boolean receiverRegistered = false, foreground = true;
    private volatile long session = 0;
    private final java.util.concurrent.ExecutorService rootTasks=java.util.concurrent.Executors.newSingleThreadExecutor();
    private NetHunterCapture netHunter;
    private volatile boolean rawMode=false, attack=false;
    private final java.util.concurrent.atomic.AtomicInteger queued=new java.util.concurrent.atomic.AtomicInteger();
    private long wifiPackets=0, blePackets=0, decodeRejects=0;
    private long captureGeneration=0,captureDeadline=0;
    private final java.util.concurrent.ConcurrentHashMap<Integer,Long> frequencies=new java.util.concurrent.ConcurrentHashMap<>();
    private final java.util.LinkedHashMap<String,JSObject> pending=new java.util.LinkedHashMap<>();
    private final Runnable flush=new Runnable(){public void run(){
        if(!running)return;
        JSArray rows=new JSArray(); for(JSObject row:pending.values())rows.put(row);pending.clear();
        JSObject event=new JSObject();event.put("targets",rows);event.put("session",session);event.put("wifi",wifiPackets);event.put("ble",blePackets);event.put("rejects",decodeRejects);
        if(netHunter!=null&&netHunter.isRecording()){long[] count=netHunter.counts();event.put("capture_wifi",count[0]);event.put("capture_ble",count[1]);event.put("remain",captureDeadline==0?0:Math.max(0,(captureDeadline-SystemClock.elapsedRealtime())/1000));}
        notifyListeners("observations",event);
        handler.postDelayed(this,500);
    }};
    private void decoded(String json,int frequency) throws org.json.JSONException {
        org.json.JSONArray rows=new org.json.JSONArray(json);
        for(int i=0;i<rows.length();i++){JSObject row=JSObject.fromJSONObject(rows.getJSONObject(i));row.put("decoded",true);row.put("source",rawMode?"nethunter":"android-phone");row.put("observed_at",System.currentTimeMillis());if(frequency>0)row.put("freq",frequency);pending.put(row.getString("mac"),row);}
        while(pending.size()>256){pending.remove(pending.keySet().iterator().next());decodeRejects++;}
    }
    private byte[] apFrame(android.net.wifi.ScanResult r) {
        byte[] ssid=(r.SSID==null?"":r.SSID).getBytes(java.nio.charset.StandardCharsets.UTF_8);
        if(ssid.length>32)ssid=java.util.Arrays.copyOf(ssid,32);
        byte[] p=new byte[38+ssid.length];p[0]=(byte)0x80;java.util.Arrays.fill(p,4,10,(byte)255);
        String[] a=r.BSSID.split(":");for(int i=0;i<6;i++){p[10+i]=(byte)Integer.parseInt(a[i],16);p[16+i]=p[10+i];}
        p[36]=0;p[37]=(byte)ssid.length;System.arraycopy(ssid,0,p,38,ssid.length);return p;
    }
    private final Runnable wifiPoll = new Runnable() {
        @Override public void run() {
            if (!running) return;
            try {
                if (!wifi.startScan()) status("Wi-Fi scan throttled or unavailable; waiting for fresh results");
            } catch (SecurityException e) { status("Wi-Fi permission was revoked"); }
            handler.postDelayed(this, 35000); // Android foreground limit: 4 / 2 min
        }
    };
    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            if (!running || rawMode || !intent.getBooleanExtra(WifiManager.EXTRA_RESULTS_UPDATED, false)) return;
            try {
                long nowUs = SystemClock.elapsedRealtimeNanos() / 1000;
                JSArray rows = new JSArray();
                for (android.net.wifi.ScanResult r : wifi.getScanResults()) {
                    long ageMs = (nowUs - r.timestamp) / 1000;
                    if (ageMs < 0 || ageMs > 8000 || r.BSSID == null) continue;
                    JSObject row = new JSObject();
                    row.put("mac", r.BSSID); row.put("name", r.SSID); row.put("ssid", r.SSID);
                    row.put("rssi", r.level); row.put("protocol", "WiFi"); row.put("ap", 1);
                    row.put("freq", r.frequency); row.put("observed_at", System.currentTimeMillis() - ageMs);
                    int ch=r.frequency==2484?14:r.frequency<2500?(r.frequency-2407)/5:r.frequency<5925?(r.frequency-5000)/5:(r.frequency-5950)/5;
                    try{decoded(NativeDetector.feed(true,r.BSSID,r.level,ch,apFrame(r),0,SystemClock.elapsedRealtime(),attack),r.frequency);}catch(Exception e){decodeRejects++;}
                }
                JSObject event = new JSObject(); event.put("targets", rows); event.put("session", session);
                // Decoded rows are emitted by the bounded flush.
            } catch (SecurityException e) { status("Wi-Fi permission was revoked"); }
        }
    };
    private ScanCallback callback;
    private ScanCallback createCallback(final long generation) { return new ScanCallback() {
        @Override public void onScanResult(int type, ScanResult result) {
            handler.post(() -> {
                if (!running || generation != session || result.getScanRecord() == null) return;
                try {
                    byte[] raw=result.getScanRecord().getBytes();
                    int address=Build.VERSION.SDK_INT>=35?result.getDevice().getAddressType():-1;
                    // Unknown address type never fabricates a random-address spam signal.
                    int detectorAddress=address==android.bluetooth.BluetoothDevice.ADDRESS_TYPE_RANDOM?1:address==android.bluetooth.BluetoothDevice.ADDRESS_TYPE_PUBLIC?0:-1;
                    blePackets++;
                    decoded(NativeDetector.feed(false,result.getDevice().getAddress(),result.getRssi(),0,raw,detectorAddress,SystemClock.elapsedRealtime(),attack),0);
                    if(netHunter!=null){JSObject row=new JSObject();row.put("mac",result.getDevice().getAddress());row.put("rssi",result.getRssi());row.put("address_type",address);row.put("observed_at",System.currentTimeMillis());row.put("raw_advertisement",Base64.encodeToString(raw,Base64.NO_WRAP));try{netHunter.ble(row.toString());}catch(java.io.IOException e){status("BLE capture write failed: "+e.getMessage());}}
                } catch (Exception e) { stopInternal(); status("Bluetooth scan/decoder failed: "+e.getMessage()); }
            });
        }
        @Override public void onScanFailed(int code) {
            handler.post(() -> { if(generation != session) return; stopInternal(); status("Bluetooth scan failed: " + code); });
        }
    }; }
    private void status(String message) {
        JSObject event = new JSObject(); event.put("message", message); event.put("running", running);
        event.put("session", session); notifyListeners("state", event);
    }
    @PluginMethod public void start(PluginCall call) {
        if (getPermissionState("location") != PermissionState.GRANTED) {
            requestPermissionForAlias("location", call, "locationReady"); return;
        }
        locationReady(call);
    }
    @PluginMethod public void authorizeRoot(PluginCall call) {
        rootTasks.execute(()->{try{
            Process check=new ProcessBuilder("su","-c","id -u").start();
            if(!check.waitFor(40,java.util.concurrent.TimeUnit.SECONDS)){check.destroy();call.reject("NetHunter root permission timed out");return;}
            java.io.BufferedReader result=new java.io.BufferedReader(new java.io.InputStreamReader(check.getInputStream()));
            String uid=result.readLine();if(uid==null)uid="";uid=uid.trim();
            if(check.exitValue()!=0||!uid.equals("0")){call.reject("Grant SignalSweep root access in Magisk for NetHunter scanning");return;}
            call.resolve();
        }catch(Exception e){call.reject("NetHunter root access failed: "+e.getMessage());}});
    }
    @PermissionCallback private void locationReady(PluginCall call) {
        if (getPermissionState("location") != PermissionState.GRANTED) {
            call.reject("Precise location permission is required by Android Wi-Fi scanning. No GPS coordinates are collected."); return;
        }
        if (Build.VERSION.SDK_INT >= 31 && getPermissionState("bluetooth") != PermissionState.GRANTED) {
            requestPermissionForAlias("bluetooth", call, "bluetoothReady"); return;
        }
        bluetoothReady(call);
    }
    @PermissionCallback private void bluetoothReady(PluginCall call) {
        if (Build.VERSION.SDK_INT >= 31 && getPermissionState("bluetooth") != PermissionState.GRANTED) {
            call.reject("Nearby devices permission is required for phone BLE scanning"); return;
        }
        handler.post(() -> {
            try {
                if (!foreground) { call.reject("Return to the app and tap Phone scan to start"); return; }
                stopInternal();
                rawMode=Boolean.TRUE.equals(call.getBoolean("nethunter",false));attack=false;
                NativeDetector.reset(call.getString("rules","[]"));
                wifiPackets=blePackets=decodeRejects=0;pending.clear();frequencies.clear();
                LocationManager location = (LocationManager) getContext().getSystemService(Context.LOCATION_SERVICE);
                if (!rawMode && !location.isProviderEnabled(LocationManager.GPS_PROVIDER) && !location.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                    call.reject("Turn on Android Location services to scan Wi-Fi. No GPS fix is requested."); return;
                }
                BluetoothManager manager = (BluetoothManager) getContext().getSystemService(Context.BLUETOOTH_SERVICE);
                BluetoothAdapter adapter = manager == null ? null : manager.getAdapter();
                if (adapter == null || !adapter.isEnabled()) { call.reject("Turn on Bluetooth for phone scanning"); return; }
                ble = adapter.getBluetoothLeScanner();
                if (ble == null) { call.reject("Phone BLE scanner is unavailable"); return; }
                wifi = (WifiManager) getContext().getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                if (wifi == null) { call.reject("Phone Wi-Fi scanner is unavailable"); return; }
                IntentFilter filter = new IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
                if (Build.VERSION.SDK_INT >= 33) getContext().registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
                else getContext().registerReceiver(receiver, filter);
                receiverRegistered = true; running = true; ++session;
                getActivity().getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                callback = createCallback(session);
                ScanSettings.Builder builder=new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY);
                if(Build.VERSION.SDK_INT>=26)builder.setLegacy(false).setPhy(ScanSettings.PHY_LE_ALL_SUPPORTED);
                ScanSettings settings = builder.build();
                ble.startScan(Collections.emptyList(), settings, callback);
                handler.post(flush);
                if(rawMode){
                    netHunter=new NetHunterCapture();final long generation=session;
                    final NetHunterCapture owner=netHunter;
                    rootTasks.execute(()->{if(!running||session!=generation)return;try{owner.start(getContext(),new NetHunterCapture.Listener(){
                        public void ready(){handler.post(()->{if(!running||session!=generation)return;JSObject answer=new JSObject();answer.put("session",session);answer.put("running",true);answer.put("nethunter",true);call.resolve(answer);status("NetHunter raw Wi-Fi + BLE scan. Wi-Fi connection released until Stop.");});}
                        public void packet(PcapReader.Packet p){if(!running||session!=generation)return;wifiPackets++;if(p.frequency>0)frequencies.merge(p.frequency,1L,Long::sum);if(p.malformed){decodeRejects++;return;}if(p.badFcs||p.frame.length<24||(p.frame[0]&12)!=0)return;if(queued.incrementAndGet()>256){queued.decrementAndGet();decodeRejects++;return;}try{String json=NativeDetector.feed(true,"",p.rssi,p.channel,p.frame,0,SystemClock.elapsedRealtime(),attack);handler.post(()->{try{if(running&&session==generation)decoded(json,p.frequency);}catch(Exception e){decodeRejects++;}finally{queued.decrementAndGet();}});}catch(Exception e){queued.decrementAndGet();decodeRejects++;}}
                        public void ended(String message){handler.post(()->{if(session!=generation)return;stopInternal();call.reject(message);status(message);});}
                    });}catch(Exception e){handler.post(()->{if(session==generation){stopInternal();call.reject("NetHunter start failed: "+e.getMessage());}});}});
                    return;
                }
                handler.post(wifiPoll);
                JSObject answer = new JSObject(); answer.put("session", session); answer.put("running", true);
                call.resolve(answer);
            } catch (Exception e) { stopInternal(); call.reject("Phone scan could not start: " + e.getMessage()); }
        });
    }
    @PluginMethod public void stop(PluginCall call) {
        handler.post(() -> { stopInternal(); rootTasks.execute(()->call.resolve()); });
    }
    private void stopInternal() {
        running = false; ++session; handler.removeCallbacks(wifiPoll);handler.removeCallbacks(flush);pending.clear();
        getActivity().getWindow().clearFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        ++captureGeneration;
        if(netHunter!=null){NetHunterCapture previous=netHunter;netHunter=null;rootTasks.execute(()->{try{if(previous.isRecording())savedCapture(previous);}catch(Exception e){status("Capture save failed: "+e.getMessage());}finally{previous.stop();}});}
        if (ble != null && callback != null) {
            try { ble.stopScan(callback); } catch (SecurityException ignored) {}
            ble = null;
        }
        if (receiverRegistered) { getContext().unregisterReceiver(receiver); receiverRegistered = false; }
    }
    @PluginMethod public void configure(PluginCall call){try{String rules=call.getString("rules",null);if(rules!=null)NativeDetector.reset(rules);attack=Boolean.TRUE.equals(call.getBoolean("attack",false));call.resolve();}catch(Exception e){call.reject(e.getMessage());}}
    @PluginMethod public void captureStart(PluginCall call){
        handler.post(()->{try{if(netHunter==null)throw new java.io.IOException("Select NetHunter raw scan first");int seconds=call.getInt("seconds",300);if(seconds<0||seconds>3600)throw new java.io.IOException("Capture duration must be 0 through 3600 seconds");netHunter.beginRecording(getContext());captureDeadline=seconds==0?0:SystemClock.elapsedRealtime()+seconds*1000L;final NetHunterCapture owner=netHunter;final long generation=++captureGeneration;if(seconds>0)handler.postDelayed(()->{if(netHunter==owner&&captureGeneration==generation){try{finishCapture();}catch(Exception e){status("Capture save failed: "+e.getMessage());}}},seconds*1000L);JSObject out=new JSObject();out.put("recording",true);call.resolve(out);}catch(Exception e){call.reject(e.getMessage());}});
    }
    private JSObject finishCapture()throws java.io.IOException{
        ++captureGeneration;return savedCapture(netHunter);
    }
    private JSObject savedCapture(NetHunterCapture owner)throws java.io.IOException{
        JSObject out=new JSObject();out.put("path",owner.endRecording());out.put("ble_path",owner.blePath());long[] counts=owner.counts();out.put("wifi",counts[0]);out.put("ble",counts[1]);out.put("bytes",counts[2]);notifyListeners("captureSaved",out);return out;
    }
    @PluginMethod public void captureStop(PluginCall call){handler.post(()->{try{if(netHunter==null)throw new java.io.IOException("No NetHunter scan");call.resolve(finishCapture());}catch(Exception e){call.reject(e.getMessage());}});}
    @PluginMethod public void stats(PluginCall call){JSObject out=new JSObject();out.put("running",running);out.put("nethunter",rawMode);out.put("wifi",wifiPackets);out.put("ble",blePackets);out.put("rejects",decodeRejects);out.put("session",session);JSObject freq=new JSObject();for(java.util.Map.Entry<Integer,Long> f:frequencies.entrySet())freq.put(String.valueOf(f.getKey()),f.getValue());out.put("frequencies",freq);call.resolve(out);}
    @PluginMethod public void shareCapture(PluginCall call){
        try{java.io.File dir=getContext().getExternalFilesDir("captures");java.util.ArrayList<android.net.Uri> files=new java.util.ArrayList<>();
            for(String key:new String[]{"path","ble_path"}){String path=call.getString(key,"");java.io.File file=new java.io.File(path).getCanonicalFile();if(dir==null||!file.getParentFile().equals(dir.getCanonicalFile())||!file.isFile())throw new java.io.IOException("Capture file is unavailable");files.add(androidx.core.content.FileProvider.getUriForFile(getContext(),getContext().getPackageName()+".fileprovider",file));}
            Intent intent=new Intent(Intent.ACTION_SEND_MULTIPLE);intent.setType("application/octet-stream");intent.putParcelableArrayListExtra(Intent.EXTRA_STREAM,files);intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);getActivity().startActivity(Intent.createChooser(intent,"Export SignalSweep capture"));call.resolve();
        }catch(Exception e){call.reject(e.getMessage());}
    }
    @PluginMethod public void listCaptures(PluginCall call){
        try{java.io.File dir=getContext().getExternalFilesDir("captures");JSArray rows=new JSArray();java.io.File[] files=dir==null?null:dir.listFiles((parent,name)->name.endsWith(".pcap"));
            if(files!=null){java.util.Arrays.sort(files,(a,b)->Long.compare(b.lastModified(),a.lastModified()));for(java.io.File file:files){if(netHunter!=null&&file.getAbsolutePath().equals(netHunter.recordingPath()))continue;java.io.File bleFile=new java.io.File(dir,file.getName().replace(".pcap","-ble.jsonl"));if(!bleFile.isFile()||file.length()<24)continue;JSObject row=new JSObject();row.put("path",file.getAbsolutePath());row.put("ble_path",bleFile.getAbsolutePath());row.put("name",file.getName());row.put("bytes",file.length());row.put("modified",file.lastModified());rows.put(row);if(rows.length()>=100)break;}}
            JSObject out=new JSObject();out.put("captures",rows);call.resolve(out);
        }catch(Exception e){call.reject(e.getMessage());}
    }
    @PluginMethod public void replayFixtures(PluginCall call){
        if(!BuildConfig.DEBUG){call.reject("Fixture replay is available only in debug builds");return;}
        if(running){call.reject("Stop scanning before replaying fixtures");return;}
        rootTasks.execute(()->{try{
            java.io.ByteArrayOutputStream bytes=new java.io.ByteArrayOutputStream();
            try(java.io.InputStream in=getContext().getAssets().open("decoder-fixtures.json")){byte[] b=new byte[4096];int n;while((n=in.read(b))!=-1)bytes.write(b,0,n);}
            org.json.JSONArray cases=new org.json.JSONArray(bytes.toString("UTF-8"));JSArray failures=new JSArray();int passed=0;
            for(int i=0;i<cases.length();i++){
                org.json.JSONObject fixture=cases.getJSONObject(i);NativeDetector.reset(fixture.getJSONArray("rules").toString());org.json.JSONArray steps=fixture.getJSONArray("steps"),rows=new org.json.JSONArray();
                for(int j=0;j<steps.length();j++){org.json.JSONObject step=steps.getJSONObject(j);String hex=step.getString("hex");byte[] raw=new byte[hex.length()/2];for(int k=0;k<raw.length;k++)raw[k]=(byte)Integer.parseInt(hex.substring(k*2,k*2+2),16);
                    rows=new org.json.JSONArray(NativeDetector.feed(step.getString("radio").equals("WiFi"),step.getString("mac"),step.getInt("rssi"),step.getInt("ch"),raw,step.optInt("address_type",0),step.getLong("at"),step.optBoolean("attack",false)));
                }
                org.json.JSONObject expect=fixture.getJSONObject("expect"),selected=null;
                for(int j=0;j<rows.length();j++){org.json.JSONObject row=rows.getJSONObject(j);if(expect.has("mac")){if(expect.getString("mac").equals(row.getString("mac")))selected=row;}else if(selected==null||row.optInt("confidence")>selected.optInt("confidence"))selected=row;}
                boolean ok=expect.optBoolean("absent",false)?selected==null:selected!=null;
                java.util.Iterator<String> keys=expect.keys();while(keys.hasNext()){String key=keys.next();if(key.equals("absent"))continue;if(selected==null||!expect.get(key).toString().equals(selected.opt(key)==null?"null":selected.opt(key).toString()))ok=false;}
                if(ok)passed++;else failures.put(fixture.getString("id"));
            }
            JSObject out=new JSObject();out.put("passed",passed);out.put("total",cases.length());out.put("failures",failures);call.resolve(out);
        }catch(Exception e){call.reject("Fixture replay failed: "+e.getMessage());}});
    }
    @Override protected void handleOnPause() { foreground=false; stopInternal(); status("Phone scan stopped when app left the foreground. Tap Phone scan to restart."); }
    @Override protected void handleOnResume() { foreground=true; }
    @Override protected void handleOnDestroy() { stopInternal(); }
}
