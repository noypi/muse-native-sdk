'use strict';
// @muse/cli — `muse-native egl-probe` thin passthrough (FR-GRAPHICS-011, AC-031).
// Builds (if needed) then execs the per-ABI probe binary matching the target
// via engine/graphics/egl-probe/run-eprobe.sh, forwarding args/stdout/stderr
// unchanged and exiting with the probe's exit code. No EGL in packages
// (FR-PKG-007): the engine probe binary owns EGL.
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function runEglProbe(rest) {
  const args = Array.isArray(rest) ? rest.slice() : [];
  const script = path.resolve(__dirname, '..', '..', '..', 'engine', 'graphics', 'egl-probe', 'run-eprobe.sh');
  const res = spawnSync('sh', [script, ...args], { stdio: 'inherit' });
  if (res.error) {
    process.stderr.write(`Error: egl-probe failed to run: ${res.error.message || res.error}\n`);
    process.exitCode = 1;
    return;
  }
  if (typeof res.status === 'number') {
    process.exitCode = res.status;
    return;
  }
  process.exitCode = 1;
}

module.exports = runEglProbe;
module.exports.run = runEglProbe;
