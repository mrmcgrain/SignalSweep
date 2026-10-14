// SPDX-License-Identifier: GPL-3.0-or-later
// Shared verbatim by ESP32 radio callbacks and the Android native adapter.
#define W_OUI    30
#define W_MFG    45
#define W_NAME   70
#define W_UUID   70
#define W_WIFI_OUI 30
// ponytail: Lite-On vendor IE (50:6F:9A) is only a weak hint — 50:6F:9A is
// Lite-On Technology's general OUI, present in many consumer WiFi chips, not
// Flock-specific. Bench testing flagged 3 random nearby devices as "Confirmed"
// when this was 80. Keep it weak; real confidence needs the SSID or corroboration.
#define W_WIFI_IE  30
#define W_WIFI_SSID 80
// A wildcard probe request (SSID IE tag 0, length 0) is worthless on its own --
// every phone in range sends them constantly, which is exactly why this is
// gated on the OUI having matched a *Flock* rule. The two together are two
// independent facts at once, and it is the only signal a current camera still
// emits: the management AP was deactivated ~Dec 2025 (taking W_WIFI_SSID with
// it) and BLE stopped working in spring 2026 (taking the name and mfg rules).
// Before this, a camera scored W_WIFI_OUI alone = 30 -- under CONF_LIST_MIN, so
// not even listed, let alone sounded. At 70 it clears CONF_ALERT_MIN on its
// own, which is the whole point: anything lower and the detector stays silent
// in front of the hardware it is named after.
// Research: DeFlockJoplin, via colonelpanichacks/flock-you. Drive-tested in
// Joplin at 11 of 12 cameras caught with 2 false positives.
#define W_WIFI_PROBE   70
// The same, plus the exact vendor IE payload (221, len 7, 50:6f:9a:16:03:01:03).
// Only ever a tightener on that path, never standalone: consumer WiFi sends it
// too (see the scoring block). This is the discriminating core of
// upstream's FLOCK_PROBE_IE_SIG_PRIMARY; their full ~200-line fingerprint
// builder (TLV resync, phantom-overflow recovery, a signature string compared
// against one hardcoded allowlist entry) buys very little over these 7 bytes
// and is tied to a single camera firmware revision.
#define W_WIFI_IE_SIG  80
#define W_CORROBORATION 15   // same MAC seen on both BLE and WiFi
// Protocol matches are unambiguous: an ASTM Remote-ID beacon IS a drone, an
// Apple Find My "offline finding" advert IS a tracker. Presence alone is the
// detection — no need to decode operator GPS just to sound the buzzer.
#define W_DRONE      90
#define W_TRACKER    80
// Flock's solar battery pack. Two facts at once (0x09C8 + a serial, or the full
// "Penguin-<10 digits>" name), so it identifies on its own. See blePenguin().
#define W_PENGUIN    80
// Pwnagotchi (pwngrid) presence: a beacon from the fixed source MAC
// de:ad:be:ef:de:ad carrying JSON in vendor IE id 222. The JSON fields
// (pwnd_tot/pwnd_run/grid_version) are the real tell — the MAC is trivially
// editable, so it is only corroboration. Strong: it identifies on its own.
#define W_PWNGRID    90
#define W_ATTACK 90   // attack-detector rows: above CONF_ALERT_MIN, generic beep
// A single weak, non-Flock-specific signal (a broad OUI prefix, or the Lite-On
// vendor IE that rides countless consumer WiFi chips) is noise on its own. Only
// list a device that clears 60 — i.e. one specific signal (SSID/UUID/name at
// 70-80) or two corroborating weak ones (OUI 30 + IE 30, or +15 cross-protocol).
// ponytail: this is the noise floor; lower it if real devices are being missed.
// Overridable at build time (-D CONF_LIST_MIN=0) so a bench test can see the
// full unfiltered harvest -- e.g. comparing antennas across two boards.
#ifndef CONF_LIST_MIN
#define CONF_LIST_MIN   60
#endif

// Listing and alerting are different questions and must not share a threshold.
// Confidence is a SUM, so two individually-meaningless hints add up to a
// specific-looking score: a consumer device on a listed OUI (30) that also
// carries the Lite-On vendor IE (30) lands on exactly 60 and used to sound the
// buzzer. Field-confirmed: the alarm fired repeatedly on a route with no ALPR
// anywhere near it. So the buzzer now tests the STRONGEST SINGLE signal
// (`bestWeight`), not the sum — it must be one signal that identifies something
// on its own (SSID 80, device name 70, service UUID 70), never a pile of
// generic ones. The sum still drives the list and the tier, where being
// generous is free.
#define CONF_ALERT_MIN  70

// Now that the mode harvests everything rather than only signature hits, the
// target list needs the same lifecycle every other mode already had.
#define WATCHERS_STALE_MS    120000  // drop devices unheard for 2 min
// How long the hunted target may go unheard before the clicker falls silent.
// Long enough to survive a few missed advertising intervals while you turn a
// corner; short enough that walking out of range stops the noise promptly.
#define HUNT_SILENCE_MS 8000
#ifdef SIGNALSWEEP_CARDPUTER_ADV
#define WATCHERS_MAX_REPORT  12
#define WATCHERS_MAX_REPORT_ALL 8
#else
#define WATCHERS_MAX_REPORT  40      // per-push cap; selection is round-robin
#define WATCHERS_MAX_REPORT_ALL 18   // per-push cap while the filter is off
#endif

static String tierForConfidence(int confidence) {
    if (confidence >= 75) return "Confirmed";
    if (confidence >= 45) return "Likely";
    return "Possible";
}

