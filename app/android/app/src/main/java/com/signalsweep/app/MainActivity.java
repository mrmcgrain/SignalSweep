package com.signalsweep.app;

import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    @Override public void onCreate(android.os.Bundle state) {
        registerPlugin(PhoneScannerPlugin.class);
        super.onCreate(state);
    }
}
