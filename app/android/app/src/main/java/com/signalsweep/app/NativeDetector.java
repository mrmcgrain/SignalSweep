package com.signalsweep.app;
/** Calls the same detector callback source compiled into the ESP32 firmware. */
final class NativeDetector {
    static { System.loadLibrary("signalsweep_detector"); }
    private static native void resetRaw(byte[] rules);
    static void reset(String rules){resetRaw(rules.getBytes(java.nio.charset.StandardCharsets.UTF_8));}
    private static native byte[] feedRaw(boolean wifi,String mac,int rssi,int channel,byte[] packet,int addressType,long now,boolean attack);
    static String feed(boolean wifi,String mac,int rssi,int channel,byte[] packet,int addressType,long now,boolean attack){return new String(feedRaw(wifi,mac,rssi,channel,packet,addressType,now,attack),java.nio.charset.StandardCharsets.UTF_8);}
}
