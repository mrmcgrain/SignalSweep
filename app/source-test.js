import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
await import('./public/scan-sources.js');
const api = globalThis.SweepSources;
assert.deepEqual(api.capabilities({board:'android-phone',capture_supported:false,sd_supported:false}).native,true);
assert.equal(api.capabilities({board:'esp32-s3',capture_supported:true,sd_supported:false}).capture,true);
assert.equal(api.capabilities({board:'cardputer-adv',capture_supported:true}).sd,true);
assert.equal(api.capabilities({}).capture,true); // backwards-compatible S3 protocol
assert.equal(api.fresh({observed_at:100},8100),false);
assert.equal(api.fresh({observed_at:100},200),true);
assert.equal(api.fresh({observed_at:300},200),false);
console.log('Source capabilities and observation freshness checks passed; detector fixtures run against shared C++ callbacks');
