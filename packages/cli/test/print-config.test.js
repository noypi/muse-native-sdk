'use strict';
// TC-WORKFLOW-001 (T1: FR-WORKFLOW-001, AC-WORKFLOW-001) — Batch 1.
// `muse-native build --print-config` prints the fully resolved configuration
// as JSON, and that output is exactly what the build uses.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns
// `node bin/muse-native.js` against fs.mkdtemp dirs; env is passed per-spawn
// (never mutates process.env); no network, no secrets, no live services.
// Flag position follows the plan: `build <appDir> --print-config` (flags
// after the positional <appDir>).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1a: the schema owns at least `name: string` and `version: string`
//   (the keys in the scaffolded `muse-native.yaml` template).
// - A-T1b: `--print-config` writes ONLY the JSON document to stdout so the
//   trimmed stdout parses as JSON.

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

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

function writeYaml(appDir, text) {
  fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), text, 'utf8');
}

function printConfig(appDir, options) {
  return runCli(['build', appDir, '--print-config'], options);
}

function parsePrinted(stdout) {
  const parsed = JSON.parse(stdout.trim());
  assert.ok(parsed && typeof parsed === 'object', 'output must be a JSON object');
  assert.ok(parsed.config && typeof parsed.config === 'object', 'output must carry a `config` object');
  assert.ok(
    parsed.winningLayer && typeof parsed.winningLayer === 'object',
    'output must carry a `winningLayer` object',
  );
  return parsed;
}

describe('TC-WORKFLOW-001 — build --print-config prints the resolved config as JSON', () => {
  it('prints { config, winningLayer } JSON with exit 0 for a project with a config file', () => {
    const appDir = mkTmp('muse-print-');
    writeYaml(appDir, 'name: printed-app\nversion: 0.0.1\n');
    const r = printConfig(appDir);
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = parsePrinted(r.stdout);
    assert.equal(parsed.config.name, 'printed-app', 'printed config must carry the file value');
  });

  it('printed output is exactly what the build uses: deterministic across runs', () => {
    const appDir = mkTmp('muse-print-');
    writeYaml(appDir, 'name: deterministic-app\nversion: 0.0.1\n');
    const first = parsePrinted(printConfig(appDir).stdout);
    const second = printConfig(appDir);
    assert.equal(second.status, 0, `--print-config must exit 0, got ${second.status}: ${second.stderr}`);
    assert.deepEqual(parsePrinted(second.stdout), first, 'repeated runs must print the identical resolution');
  });

  it('every resolved key names its winning layer in winningLayer', () => {
    const appDir = mkTmp('muse-print-');
    writeYaml(appDir, 'name: layered-app\nversion: 0.0.1\n');
    const r = printConfig(appDir);
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = parsePrinted(r.stdout);
    for (const key of Object.keys(parsed.config)) {
      assert.ok(
        ['cli', 'env', 'file', 'default'].includes(parsed.winningLayer[key]),
        `winningLayer['${key}'] must name cli|env|file|default, got '${parsed.winningLayer[key]}'`,
      );
    }
  });

  it('with no config file, keys resolve to built-in defaults with layer "default"', () => {
    const appDir = mkTmp('muse-print-');
    const r = printConfig(appDir);
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = parsePrinted(r.stdout);
    assert.ok(Object.keys(parsed.config).length > 0, 'resolved config must carry at least one key');
    for (const key of Object.keys(parsed.config)) {
      assert.equal(
        parsed.winningLayer[key],
        'default',
        `with no config file, winningLayer['${key}'] must be 'default'`,
      );
    }
  });

  it('a file value wins over the default and is reported with layer "file"', () => {
    const appDir = mkTmp('muse-print-');
    writeYaml(appDir, 'name: from-file\n');
    const r = printConfig(appDir);
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = parsePrinted(r.stdout);
    assert.equal(parsed.config.name, 'from-file', 'file value must win over the default');
    assert.equal(parsed.winningLayer.name, 'file', 'winning layer for a file-set key must be "file"');
  });

  it('the scaffolded template documents its defaults as comments', () => {
    const text = fs.readFileSync(path.join(CLI_DIR, 'templates', 'muse-native.yaml'), 'utf8');
    const commented = text.split('\n').filter((line) => line.trim().startsWith('#'));
    assert.ok(commented.length > 0, 'template must carry commented defaults');
    assert.ok(/default/i.test(commented.join('\n')), 'commented lines must state the defaults');
  });
});
