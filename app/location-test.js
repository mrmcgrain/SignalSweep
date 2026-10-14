import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
const noop = () => {};
const ctx = { console, TextDecoder, TextEncoder, setTimeout: () => 0, clearTimeout: noop, setInterval: () => 0,
    localStorage: { getItem: () => null }, navigator: {},
    document: { addEventListener: noop, getElementById: () => null, querySelectorAll: () => [] },
    Capacitor: { isNativePlatform: () => true },
    BleClient: { isLocationEnabled: async () => true } };
ctx.window = ctx;
vm.createContext(ctx);
vm.runInContext(readFileSync(new URL('./public/app.js', import.meta.url), 'utf8'), ctx);
ctx.Geolocation = { getCurrentPosition: async options => {
    assert.equal(options.timeout, 60000);
    assert.equal(options.maximumAge, 0);
    assert.equal(options.enableHighAccuracy, true);
    return { coords: { latitude: 1, longitude: 2, accuracy: 12 } };
} };
assert.equal((await vm.runInContext('getFix()', ctx)).acc, 12);
assert.equal(vm.runInContext('gpsState.state', ctx), 'fix');
for (const [code, state] of [['OS-PLUG-GLOC-0010', 'timeout'], [3, 'timeout'],
    [1, 'denied'], ['OS-PLUG-GLOC-0007', 'sysoff']]) {
    ctx.Geolocation.getCurrentPosition = async () => { throw { code }; };
    await assert.rejects(vm.runInContext('getFix()', ctx));
    assert.equal(vm.runInContext('gpsState.state', ctx), state);
}
let requested = false;
ctx.BleClient.isLocationEnabled = async () => false;
ctx.BleClient.openLocationSettings = async () => {};
ctx.Geolocation.getCurrentPosition = async () => { requested = true; };
// Suppress unrelated toast rendering for the disabled-services case.
vm.runInContext('showToast = () => {}', ctx);
await assert.rejects(vm.runInContext('getFix()', ctx));
assert.equal(requested, false);
assert.equal(vm.runInContext('gpsState.state', ctx), 'sysoff');
delete ctx.Geolocation;
ctx.navigator.geolocation = { getCurrentPosition: (done, failed, options) => {
    assert.equal(options.timeout, 60000);
    assert.equal(options.maximumAge, 0);
    failed({ code: 3 });
} };
await assert.rejects(vm.runInContext('getFix()', ctx));
assert.equal(vm.runInContext('gpsState.state', ctx), 'timeout');
console.log('One-shot location options, success, timeout, permission and disabled-services checks passed');
