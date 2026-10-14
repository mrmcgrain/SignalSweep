// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace capture_buffer {
constexpr size_t MAX_BYTES = 256, HEADER_BYTES = 15;
struct Record {
    uint32_t seq, ts;
    uint16_t original, length;
    uint8_t radio, channel;
    int8_t rssi;
    uint8_t data[MAX_BYTES];
};
// Caller serializes access. Full queues drop the incoming record, preserving
// earlier records and leaving a sequence gap with an explicit drop counter.
template<size_t N> struct Queue {
    Record slots[N];
    size_t head = 0, tail = 0, used = 0;
    uint32_t seen = 0, dropped = 0;
    void reset() { head = tail = used = 0; seen = dropped = 0; }
    bool push(uint8_t radio, uint32_t ts, uint8_t ch, int8_t rssi,
              const uint8_t* data, size_t available, uint16_t original) {
        uint32_t seq = seen++;
        if (used == N) { ++dropped; return false; }
        Record& r = slots[head];
        r.seq = seq; r.ts = ts; r.radio = radio; r.channel = ch;
        r.rssi = rssi; r.original = original;
        r.length = available > MAX_BYTES ? MAX_BYTES : available;
        memcpy(r.data, data, r.length);
        head = (head + 1) % N; ++used;
        return true;
    }
    bool pop(Record& r) {
        if (!used) return false;
        r = slots[tail]; tail = (tail + 1) % N; --used;
        return true;
    }
};
inline void le16(uint8_t* out, uint16_t n) { out[0] = n; out[1] = n >> 8; }
inline void le32(uint8_t* out, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) out[i] = n >> (8*i);
}
inline size_t encode(const Record& r, uint8_t* out) {
    out[0] = r.radio; le32(out+1, r.seq); le32(out+5, r.ts);
    out[9] = r.channel; out[10] = uint8_t(r.rssi);
    le16(out+11, r.original); le16(out+13, r.length);
    memcpy(out+HEADER_BYTES, r.data, r.length);
    return HEADER_BYTES+r.length;
}
inline bool elapsed(uint32_t now, uint32_t start, uint32_t durationMs) {
    return durationMs != 0 && uint32_t(now-start) >= durationMs;
}
} // namespace capture_buffer
