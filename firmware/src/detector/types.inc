// SPDX-License-Identifier: GPL-3.0-or-later
struct WatcherSignature {
    String name;
    String category;
    String oui;
    String mfgId;
    String deviceName;
    String serviceUuid;
    // Optional. 0 = use the per-condition weights (W_OUI, W_UUID...). Set on
    // rules whose OUI the IEEE registers to the product's own maker, so they
    // list (60) without beeping on the OUI alone.
    int weight = 0;
    // Optional. Case-sensitive SSID prefix, matched on beacons and probe
    // responses only. A rule with one is Wi-Fi-only.
    String ssidPrefix;
};

/**
 * @brief Information about a detected surveillance target
 */
struct WatcherTargetInfo {
    String mac;
    String name;
    String type;
    String matchedRule;
    String protocol;
    // The network name a Wi-Fi device is announcing. Far more identifying to a
    // human than a MAC -- "Nest_ABC" tells you what a row is at a glance, where
    // a vendor prefix does not. Beacons and probe responses only: a probe
    // REQUEST carries the network the client is looking for, not its own.
    String ssid;
    int rssi;
    uint32_t firstSeenMs;
    uint32_t lastSeenMs;
    uint32_t count;
    int confidence;   // 0-100, accumulated across matching signals
    String tier;      // "Confirmed" | "Likely" | "Possible"
    uint32_t lastReportedMs;  // round-robin cursor: oldest reported goes first
    // Has this device already sounded the buzzer since it appeared? The alert
    // belongs to the device, not to the moment it was first heard -- see the
    // comment on noteAlertForTarget() in the .cpp.
    bool alerted = false;
    // Set alongside `alerted` when an alert really sounded, cleared by the 1 Hz
    // task once the record is on flash. The target IS the log queue: it already
    // holds the mac, category, rule and rssi a record needs, it is bounded so
    // nothing can overflow, and it means no second ring and no lock shared
    // between the two radio callbacks. A LittleFS write must never happen on
    // those callbacks -- see the drain in watchersPeriodicTask().
    bool logPending = false;

    // Who made it, and which side of a Wi-Fi link it is. The app turns these
    // into a vendor name and an AP/client badge; the lookups live on the phone.
    // wifiRole: 0 unknown, 1 client (probe request), 2 AP (beacon / probe
    // response). AP is sticky -- a device that beacons is infrastructure even
    // if it also probes. bleCompany is the manufacturer-data company ID, -1
    // when none has been heard (0x0000 is a real company).
    uint8_t wifiRole   = 0;
    bool    blePublic  = false;
    int32_t bleCompany = -1;
    // Advertises Immediate Alert (0x1802) or Link Loss (0x1803): the Find Me /
    // Proximity keyfobs Ring can actually reach. Sticky, like AP.
    bool    bleRing    = false;

    // Last Wi-Fi channel this target was heard on, 0 = unknown / BLE only. The
    // app turns it into a 2.4G / 5G badge; last heard wins.
    uint8_t wifiCh     = 0;

    // Decoded ASTM F3411 Remote ID, populated only for drones (hasDrone).
    // Everything else leaves this zeroed and it never reaches the wire — the
    // 1 Hz push is the tightest budget on the board, so these fields are
    // emitted per-target only when hasDrone is set.
    // Defaults matter: the struct is filled field-by-field at both call sites
    // and has no constructor, so anything not assigned there would otherwise be
    // stack garbage — and garbage in hasDrone puts junk coordinates on the wire.
    bool   hasDrone = false;
    String uasId;                    // Basic ID / serial
    String operatorId;               // Operator ID (registration)
    String selfId;                   // free-text description the operator chose
    double droneLat = 0, droneLng = 0;  // 0/0 == no value (ODID's own "unknown")
    float  altMsl    = -1000;        // m WGS84-HAE, -1000 == unknown
    float  heightAgl = -1000;        // m above take-off, -1000 == unknown
    float  speed     = 0;            // m/s horizontal
    float  heading   = 361;          // degrees true, 361 == unknown
    double opLat = 0, opLng = 0;     // pilot/operator position, 0/0 == no value
};

