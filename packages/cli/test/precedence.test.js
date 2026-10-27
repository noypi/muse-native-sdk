'use strict';
// TC-WORKFLOW-002 (T1: FR-WORKFLOW-001, AC-WORKFLOW-002) — Batch 1.
// Precedence is CLI flag, then environment variable, then configuration
// file, then built-in default; when two layers disagree the printed
// resolution names the layer that won.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns
// `node bin/muse-native.js` against fs.mkdtemp dirs; env is passed per-spawn
// via spawnSync options (process.env is never mutated, never inherited
// implicitly for the keys under test); no network, no secrets, no services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1a: schema owns at least `name: string` (template key); the `name`
//   key stands in for every layered key in these cases.
// - A-T1b: env prefix is `MUSE_NATIVE_` and the env name is the upper-snake
//   form of the key (`name` -> `MUSE_NATIVE_NAME`), per the plan.
// - A-T1c: CLI flags are spelled `--<key> <value>` (`--name <value>`).
// - A-T1d: the deprecated alias `muse.config.ts` is a read-only fallback:
//   used only when `muse-native.yaml` is absent, in the same flat
//   `key: value` line format.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    const dir = tmpRoots.pop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

function runCli(args, extraEnv) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    env: { ...process.env, ...extraEnv },
  });
}

function printed(appDir, extraArgs, extraEnv) {
  const r = runCli(['build', appDir, '--print-config', ...(extraArgs || [])], extraEnv);
  assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
  return JSON.parse(r.stdout.trim());
}

describe('TC-WORKFLOW-002 — CLI > env > file > default precedence with named winning layers', () => {
  it('env beats file: MUSE_NATIVE_NAME overrides the yaml value and wins as "env"', () => {
    const appDir = mkTmp('muse-prec-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: from-file\n', 'utf8');
    const parsed = printed(appDir, [], { MUSE_NATIVE_NAME: 'from-env' });
    assert.equal(parsed.config.name, 'from-env', 'env value must beat the file value');
    assert.equal(parsed.winningLayer.name, 'env', 'winning layer must be "env"');
  });

  it('file beats default: yaml value is used with layer "file" when no env/CLI sets it', () => {
    const appDir = mkTmp('muse-prec-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: from-file\n', 'utf8');
    const parsed = printed(appDir);
    assert.equal(parsed.config.name, 'from-file', 'file value must beat the default');
    assert.equal(parsed.winningLayer.name, 'file', 'winning layer must be "file"');
  });

  it('CLI beats env: --name overrides MUSE_NATIVE_NAME and wins as "cli"', () => {
    const appDir = mkTmp('muse-prec-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: from-file\n', 'utf8');
    const parsed = printed(appDir, ['--name', 'from-cli'], { MUSE_NATIVE_NAME: 'from-env' });
    assert.equal(parsed.config.name, 'from-cli', 'CLI flag must beat the env value');
    assert.equal(parsed.winningLayer.name, 'cli', 'winning layer must be "cli"');
  });

  it('CLI beats file directly when no env is set', () => {
    const appDir = mkTmp('muse-prec-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: from-file\n', 'utf8');
    const parsed = printed(appDir, ['--name', 'from-cli']);
    assert.equal(parsed.config.name, 'from-cli', 'CLI flag must beat the file value');
    assert.equal(parsed.winningLayer.name, 'cli', 'winning layer must be "cli"');
  });

  it('default applies with layer "default" when no other layer sets the key', () => {
    const appDir = mkTmp('muse-prec-');
    const parsed = printed(appDir);
    assert.ok('name' in parsed.config, 'resolved config must carry the `name` key');
    assert.equal(parsed.winningLayer.name, 'default', 'winning layer must be "default"');
  });

  it('deprecated alias muse.config.ts is used only when muse-native.yaml is absent', () => {
    const appDir = mkTmp('muse-prec-');
    fs.writeFileSync(path.join(appDir, 'muse.config.ts'), 'name: from-alias\n', 'utf8');
    const parsed = printed(appDir);
    assert.equal(parsed.config.name, 'from-alias', 'alias file value must be picked up as the file layer');
    assert.equal(parsed.winningLayer.name, 'file', 'winning layer must be "file"');
  });

  it('muse-native.yaml wins over the deprecated alias when both are present', () => {
    const appDir = mkTmp('muse-prec-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: canonical\n', 'utf8');
    fs.writeFileSync(path.join(appDir, 'muse.config.ts'), 'name: from-alias\n', 'utf8');
    const parsed = printed(appDir);
    assert.equal(parsed.config.name, 'canonical', 'canonical file must beat the alias');
    assert.equal(parsed.winningLayer.name, 'file', 'winning layer must be "file"');
  });
});
