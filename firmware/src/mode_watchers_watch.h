// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 B0z0dcl0wn and the SignalSweep contributors

#ifndef MODE_WATCHERS_WATCH_H
#define MODE_WATCHERS_WATCH_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

/**
 * @brief Information about a signature rule for surveillance device detection
 */
#include "detector/types.inc"

/**
 * @brief Start Watcher's Watch mode (loads signatures, starts continuous BLE scan & target tracking)
 */
void startWatchersWatch();

/**
 * @brief Stop Watcher's Watch mode cleanly (stops BLE scan)
 */
void stopWatchersWatch();
/** ADV capture waits for detector tasks to relinquish the radio. */
bool watchersTasksStopped();

/**
 * @brief Check if Watcher's Watch mode is currently active
 * @return true if running, false otherwise
 */
bool isWatchersWatchActive();

/**
 * @brief Get JSON representation of all tracked surveillance targets
 * @return JSON string sent to the app over BLE NUS via sendBleSerial()
 */
String getWatchersTargetsJson();

/**
 * @brief Get JSON representation of all signature rules from LittleFS
 * @return JSON string sent to the app over BLE NUS via sendBleSerial()
 */
String getWatchersSignaturesJson();

/**
 * @brief Update signature database in LittleFS and reload rules in memory
 * @param jsonContent New signature JSON string or object
 * @return true if updated and reloaded successfully, false otherwise
 */
/**
 * @brief Reset the signature rules to the built-in defaults
 */
void resetWatchersSignaturesToDefaults();

bool updateWatchersSignaturesJson(const String& jsonContent);

/**
 * @brief Load signature rules from LittleFS (/data/signatures.json)
 */
void loadWatchersSignatures();

/**
 * @brief Hunt a MAC: its RSSI drives the Geiger clicker so a planted tracker
 * can be walked down by ear. Empty string clears. Detection never stops.
 */
void setHuntTarget(const String& mac);

/**
 * @brief The MAC currently being hunted, or "" if none.
 */
String getHuntTarget();

/**
 * @brief Report every tracked device rather than only signature matches, for
 * foxhunting an unlisted device. Persisted. Does NOT affect the buzzer —
 * CONF_ALERT_MIN still gates every alert.
 */
void setScanAll(bool enabled);

/**
 * @brief Reload the hunt target and report filter from NVS. Called by
 * startWatchersWatch() so the board resumes exactly what it was doing before
 * it lost power.
 */
void restoreWatchersState();

/** @brief True while the report filter is off. */
bool getScanAll();
void setAttackDetect(bool enabled);
bool getAttackDetect();

// Per-category buzzer mute. One bit per AlertCategory (hardware_manager.h), so
// bit 0 = ALERT_ALPR ... bit 4 = ALERT_GENERIC. The app mirrors this bit order
// in BEEP_BITS (app/public/app.js) — keep the two in step.
#define BEEP_MASK_ALL 0x1F

/**
 * @brief Choose which device categories are allowed to sound the buzzer.
 * Persisted, like the hunt target: the board is headless and loses power every
 * time the engine stops. A muted category is fully silent (no beep, no flash,
 * not counted by getAlertCount()) but is still tracked and still reported.
 */
void setBeepMask(uint8_t mask);

/** @brief Categories currently allowed to beep (bitmask, see BEEP_MASK_ALL). */
uint8_t getBeepMask();

/**
 * @brief Record every alert the buzzer sounds to the board's own flash.
 * Off by default and persisted (sweep-st/log), like every other operator
 * setting: a detector wired into a car must keep logging across the engine
 * stopping, or the feature only works while tethered to the phone you were
 * trying to walk away from. Follows the buzzer exactly — a muted beep_mask
 * category and an active hunt both write nothing, so the log is a faithful
 * record of what you actually heard. See alert_log.h.
 */
void setAlertLogEnabled(bool enabled);

/** @brief True while alerts are being written to flash. */
bool getAlertLogEnabled();

/**
 * @brief Wi-Fi channel the hunted target was last heard on, or 0 if unknown.
 * The channel hopper parks here while hunting -- see the comment in
 * watchersWifiChannelHopperTask().
 */
int getHuntChannel();

/**
 * @brief How many times the buzzer has sounded a category alert since boot.
 * The headless path has no other witness: with no phone attached the only
 * evidence is a noise in another room, and a serial print made seconds after
 * boot is lost while the USB CDC port re-enumerates.
 */
uint32_t getAlertCount();

/**
 * @brief Queue a "ring the tracker" for this MAC. Performed on the 1 Hz task,
 * not on the caller's stack. MAC is the only parameter by design — see
 * performRing().
 */
void requestRing(const String& mac);

#if CONFIG_IDF_TARGET_ESP32C5
/** @brief C5 only: Wi-Fi band the hopper covers (0 both, 1 2.4 GHz, 2 5 GHz). */
uint8_t getBand();
/** @brief C5 only: set and persist the band (sweep-st/band); out of range means both. */
void setBand(uint8_t band);
#endif

// Per-device Ignore: your own doorbell, dashcam or tag. Still detected, listed
// and reported; never beeps, flashes, counts or logs. Persisted (sweep-st/ignore).
#define IGNORE_MAX 16
// 0 added, 1 already there, -1 full (list at IGNORE_MAX), -2 not a MAC,
// -3 couldn't lock (mutex busy or not yet created -- distinct from -1 so a
// momentarily busy detector doesn't toast "list full").
int  addIgnore(const String& mac);
bool removeIgnore(const String& mac);
// Adds an "ignore" JsonArray to doc, but ONLY if the lock was obtained; on a
// timeout the key is left absent rather than emitting an authoritative empty
// list (an absent CMD:CFG field means "unknown", not "no ignored devices").
// [["AA:BB:..", <AlertCategory>], ...]. Returns false on timeout.
bool getIgnoreJson(JsonDocument& doc);

#ifdef SIGNALSWEEP_CARDPUTER_ADV
// Fixed-size UI snapshots. No JSON allocation or telemetry cursor changes.
struct ScannerRow {
    char mac[32];
    char name[40];
    char type[28];
    char rule[40];
    char protocol[8];
    int rssi, confidence;
    uint32_t lastSeenMs;
    uint8_t channel;
};
// False on lock timeout; caller keeps its previous snapshot and marks stale.
bool getScannerRows(ScannerRow* rows, size_t capacity, size_t& count, size_t& total);
#endif

#endif // MODE_WATCHERS_WATCH_H
