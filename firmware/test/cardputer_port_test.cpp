// SPDX-License-Identifier: GPL-3.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../src/scanner_view.h"
#include "../src/mode_capture.h"

static std::vector<String> usbReplies, bleReplies;
void sendUsbLine(const String& s) { usbReplies.push_back(s); }
void sendBleSerial(const String& s) { bleReplies.push_back(s); }

struct Target { int confidence; uint32_t lastSeenMs; bool logPending; };
struct Row { char mac[18]; int rssi; };

static void testAdmission() {
    std::vector<Target> table;
    for (size_t i = 0; i < 64; ++i) table.push_back({0, uint32_t(i), false});
    Target match{80, 100, false};
    size_t slot = scanner_view::admissionSlot(table, match, 100, 64);
    assert(slot == 0); // evicts oldest equal-score observation
    table[slot] = match;
    assert(table.size() == 64);
    assert(scanner_view::admissionSlot(table, match, 100, 64) == 1);
    for (auto& t : table) t = {80, 10, true};
    assert(scanner_view::admissionSlot(table, match, 100, 64) == SIZE_MAX); // never lose pending logs
    table[7] = {90, 10, false};
    assert(scanner_view::admissionSlot(table, match, 100, 64) == SIZE_MAX); // weaker cannot evict match
    match.confidence = 100;
    assert(scanner_view::admissionSlot(table, match, 100, 64) == 7); // correct entry, not vector.back()
    table.clear();
    assert(scanner_view::admissionSlot(table, match, 100, 64) == 0);
    table = {{80, UINT32_MAX - 5, false}, {80, 1, false}};
    assert(scanner_view::admissionSlot(table, match, 4, 2) == 0); // age across rollover
}
static void testSelection() {
    Row rows[] = {{"bb", -80}, {"aa", -90}};
    assert(scanner_view::selectedIndex(rows, 2, "aa") == 1);
    Row sorted[] = {{"aa", -40}, {"bb", -80}};
    assert(scanner_view::selectedIndex(sorted, 2, "aa") == 0); // follow MAC, not row number
    assert(scanner_view::selectedIndex(sorted, 2, "missing") == 2);
    assert(scanner_view::selectedIndex(sorted, 0, "aa") == 0); // no selection on empty list
    assert(scanner_view::insertionPoint(sorted, 2, -50, "cc") == 1);
    Row ties[] = {{"aa", -40}, {"bb", -40}};
    assert(scanner_view::insertionPoint(ties, 2, -40, "ab") == 1); // deterministic ordering
    assert(scanner_view::insertionPoint(ties, 2, -100, "cc") == 2); // full UI can discard weakest
}
static void testTransitions() {
    assert(scanner_view::keyPresses(2, 1) == 2); // same key count, different physical key
    assert(scanner_view::keyPresses(2, 2) == 0); // held key never toggles logging repeatedly
    assert(scanner_view::keyPresses(0, 2) == 0);
    assert(scanner_view::keyPresses(3, 1) == 2); // preserve held key while another is pressed
    assert(!scanner_view::expired(5, UINT32_MAX - 10, 20));
    assert(scanner_view::expired(30, UINT32_MAX - 10, 20));
}
static void testCaptureDisabled() {
    requestCapture(30, false);
    requestCapture(30, true);
    stopCapture(); captureTick();
    assert(!isCapturing() && !isCapturingToUsb());
    assert(usbReplies.size() == 2 && usbReplies == bleReplies);
    for (const auto& reply : usbReplies)
        assert(reply == "{\"cap\":{\"error\":\"packet capture disabled on this board\"}}");
}
int main() {
    testAdmission(); testSelection(); testTransitions(); testCaptureDisabled();
    std::puts("PASS: bounded targets, pending-log protection, stable selection, key transitions, clock rollover, USB/BLE capture rejection");
}
