'use strict';
// check:types gate for @muse/native (plan T2).
// Runs the workspace-local TypeScript compiler against the T1 project
// (`tsconfig.json` in this package) — never downloads a toolchain.
// Strict-vs-skip convention mirrors packages/jsx/test/props.test.ts
// (localTscBin / isStrictLiveTscRequired / liveTscGuard): a missing local
// tsc skips with a recorded message (blocked-env), unless
// MUSE_REQUIRE_LIVE_TSC=1 or CI=true is set, in which case it fails naming
// the missing tsc. `check:types` stays a separate CI gate — it is NOT wired
// into `npm test` (TC-M3-004's Check row invokes it directly).
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

function localTscBin() {
  try {
    return require.resolve('typescript/bin/tsc');
  } catch {
    // Fall through to the lib entry point below.
  }
  try {
    return require.resolve('typescript/lib/tsc.js');
  } catch {
    return path.join(
      __dirname,
      '..',
      '..',
      '..',
      'node_modules',
      '.bin',
      process.platform === 'win32' ? 'tsc.cmd' : 'tsc',
    );
  }
}

function hasLocalTsc() {
  try {
    return fs.existsSync(localTscBin());
  } catch {
    return false;
  }
}

function isStrictLiveTscRequired() {
  const on = (v) => v !== undefined && v !== '' && v !== '0' && v !== 'false';
  return on(process.env.MUSE_REQUIRE_LIVE_TSC) || on(process.env.CI);
}

function main() {
  const tscBin = localTscBin();
  if (!hasLocalTsc()) {
    if (isStrictLiveTscRequired()) {
      console.error(
        'Missing local tsc: no local TypeScript compiler resolves at ' +
          `'${tscBin}', but the run is authoritative ` +
          '(MUSE_REQUIRE_LIVE_TSC or CI is set); check:types must FAIL here, not SKIP.',
      );
      process.exit(1);
    }
    console.log(
      `SKIP check:types: no local tsc resolves at '${tscBin}' ` +
        '(blocked-env, not failed-code; set MUSE_REQUIRE_LIVE_TSC=1 or CI=true to require it).',
    );
    process.exit(0);
  }
  const project = path.join(__dirname, '..', 'tsconfig.json');
  const args = ['--noEmit', '-p', project];
  const res =
    process.platform === 'win32' && tscBin.endsWith('.cmd')
      ? spawnSync(tscBin, args, { stdio: 'inherit', shell: true })
      : spawnSync(process.execPath, [tscBin, ...args], {
          stdio: 'inherit',
        });
  if (res.error) {
    console.error(`check:types failed to launch local tsc at '${tscBin}': ${res.error.message}`);
    process.exit(1);
  }
  if (res.signal) {
    console.error(`check:types: local tsc terminated by signal ${res.signal}.`);
    process.exit(1);
  }
  process.exit(res.status ?? 1);
}

main();
