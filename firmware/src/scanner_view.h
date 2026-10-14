// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace scanner_view {
// Return the admission slot without touching any entry. Pending log records
// must be drained before their slot can be reused. SIZE_MAX means reject.
template<class Container, class Target>
size_t admissionSlot(const Container& targets, const Target& target, uint32_t now, size_t limit) {
    if (targets.size() < limit) return targets.size();
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].logPending) continue;
        if (best == SIZE_MAX || targets[i].confidence < targets[best].confidence ||
            (targets[i].confidence == targets[best].confidence &&
             uint32_t(now - targets[i].lastSeenMs) > uint32_t(now - targets[best].lastSeenMs))) best = i;
    }
    if (best == SIZE_MAX || target.confidence < targets[best].confidence) return SIZE_MAX;
    return best;
}

template<class Row>
size_t insertionPoint(const Row* rows, size_t count, int rssi, const char* mac) {
    size_t pos = 0;
    while (pos < count && (rows[pos].rssi > rssi ||
        (rows[pos].rssi == rssi && strcmp(rows[pos].mac, mac) < 0))) ++pos;
    return pos;
}

template<class Row>
size_t selectedIndex(const Row* rows, size_t count, const char* mac) {
    for (size_t i = 0; i < count; ++i) if (strcmp(rows[i].mac, mac) == 0) return i;
    return count;
}
inline uint32_t keyPresses(uint32_t held, uint32_t previous) { return held & ~previous; }
inline bool expired(uint32_t now, uint32_t since, uint32_t timeout) { return uint32_t(now - since) > timeout; }
} // namespace scanner_view
