// SPDX-License-Identifier: GPL-3.0-or-later
// Pure observation/capability helpers shared by UI and host tests.
(function (scope) {
    const uuid = value => {
        const s = String(value || '').toLowerCase();
        return s.length === 4 ? '0000' + s + '-0000-1000-8000-00805f9b34fb' : s;
    };
    function capabilities(cfg) {
        return {
            source: cfg.board || 'legacy-esp32',
            capture: cfg.capture_supported !== false,
            sd: cfg.sd_supported !== false,
            backend: cfg.capture_backend || 'legacy', maxBytes: cfg.capture_max_bytes || 256,
            wifiSlots: cfg.capture_wifi_slots, bleSlots: cfg.capture_ble_slots, logMax: cfg.log_max,
            native: cfg.board === 'android-phone'
        };
    }
    function fresh(row, now) {
        return typeof row.observed_at === 'number' && row.observed_at <= now && now - row.observed_at < 8000;
    }
    scope.SweepSources = { capabilities, fresh, uuid };
})(typeof window === 'undefined' ? globalThis : window);
