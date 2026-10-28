'use strict';
// T1 — versioned proxy config allow-list loader fails closed.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only.
// All fixtures are inline JSON written to os.tmpdir(); no network, no keys,
// no env config, no live services.

const { describe, it, afterEach, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('../lib/load-config.js');
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

let savedEnv;
beforeEach(() => {
  savedEnv = { ...process.env };
  delete process.env.MUSE_API_KEY;
  delete process.env.XAI_API_KEY;
});
afterEach(() => {
  process.env = savedEnv;
});

function validConfig() {
  return {
    version: 1,
    routes: {
      segment: {
        model: 'sam-3.1',
        params: { mask_encoding: 'polygon' },
        stream: 'none',
        timeoutMs: 8000,
        retry: 1,
        _source: 'https://example.test/q-muse-001 2026-10-28',
      },
      reason: {
        model: 'muse-spark-1.3',
        params: { reasoning_effort: 'high', include_reasoning: true, strict: true },
        stream: 'sse',
        timeoutMs: 30000,
        retry: 1,
        _source: 'https://example.test/q-muse-001 2026-10-28',
      },
      structure: {
        model: 'muse-spark-1.3',
        params: { reasoning_effort: 'high', include_reasoning: true, strict: true },
        stream: 'sse',
        timeoutMs: 30000,
        retry: 1,
        _source: 'https://example.test/q-muse-001 2026-10-28',
      },
      transcribe: {
        model: 'muse-voice-transcribe-1.0',
        params: { mode: 'PUSH_TO_TALK' },
        stream: 'sse',
        timeoutMs: 30000,
        retry: 1,
        _source: 'https://example.test/q-muse-001 2026-10-28',
      },
      'generate-image': {
        model: 'muse-image',
        params: { prompt: 'x', size: '1024x1024' },
        stream: 'progressive',
        timeoutMs: 60000,
        retry: 0,
        _source: 'https://example.test/q-muse-001 2026-10-28',
      },
    },
  };
}

function writeConfig(obj) {
  const dir = mkTmp('load-config-');
  const p = path.join(dir, 'proxy.config.json');
  fs.writeFileSync(p, JSON.stringify(obj));
  return p;
}

describe('loadConfig', () => {
  it('returns versioned route table for a valid allow-list config', () => {
    const { loadConfig } = loadImpl();
    const cfg = loadConfig(writeConfig(validConfig()));
    assert.equal(cfg.version, 1);
    assert.deepEqual(Object.keys(cfg.routes).sort(), [
      'generate-image',
      'reason',
      'segment',
      'structure',
      'transcribe',
    ]);
    assert.equal(cfg.routes.segment.model, 'sam-3.1');
    assert.equal(cfg.routes.segment.stream, 'none');
    assert.equal(cfg.routes.segment.timeoutMs, 8000);
    assert.equal(cfg.routes.segment.retry, 1);
    assert.equal(cfg.routes.reason.model, 'muse-spark-1.3');
    assert.equal(cfg.routes.reason.stream, 'sse');
    assert.equal(cfg.routes.transcribe.model, 'muse-voice-transcribe-1.0');
    assert.equal(cfg.routes['generate-image'].model, 'muse-image');
    assert.equal(cfg.routes['generate-image'].stream, 'progressive');
    assert.equal(cfg.routes['generate-image'].retry, 0);
  });

  it('exposes exactly the documented per-route timeout/retry values', () => {
    const { loadConfig } = loadImpl();
    const cfg = loadConfig(writeConfig(validConfig()));
    assert.equal(cfg.routes.reason.timeoutMs, 30000);
    assert.equal(cfg.routes.structure.timeoutMs, 30000);
    assert.equal(cfg.routes.transcribe.timeoutMs, 30000);
    assert.equal(cfg.routes['generate-image'].timeoutMs, 60000);
  });

  it('has no speak route (speak is NONE / client-side only)', () => {
    const { loadConfig } = loadImpl();
    const cfg = loadConfig(writeConfig(validConfig()));
    assert.ok(!('speak' in cfg.routes));
  });

  it('rejects an unknown route with reason unknown-route naming the key', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes.speak = { model: 'sam-3.1', params: {}, stream: 'none', timeoutMs: 1, retry: 0 };
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'unknown-route');
    assert.match(err.name, /speak/);
  });

  it('rejects a non-Muse model (gpt-4) with reason non-muse-model', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes.reason.model = 'gpt-4';
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'non-muse-model');
    assert.match(err.name, /gpt-4/);
  });

  it('rejects style_preset with reason unlisted-param (ASSUMPTION: unverified params are unlisted)', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes['generate-image'].params.style_preset = 'foo';
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'unlisted-param');
    assert.match(err.name, /style_preset/);
  });

  it('rejects seed with reason unlisted-param', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes['generate-image'].params.seed = 42;
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'unlisted-param');
    assert.match(err.name, /seed/);
  });

  it('rejects an arbitrary unlisted param with reason unlisted-param', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes.segment.params.evil = 1;
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'unlisted-param');
    assert.match(err.name, /evil/);
  });

  it('rejects a missing version with reason bad-shape', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    delete bad.version;
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'bad-shape');
  });

  it('rejects a non-numeric timeoutMs with reason bad-shape', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes.segment.timeoutMs = 'fast';
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'bad-shape');
  });

  it('rejects an invalid stream value with reason bad-shape', () => {
    const { loadConfig } = loadImpl();
    const bad = validConfig();
    bad.routes.segment.stream = 'websocket';
    let err;
    try {
      loadConfig(writeConfig(bad));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'bad-shape');
  });

  it('treats a missing file as bad-shape (ASSUMPTION: no ENOENT leak, fail closed)', () => {
    const { loadConfig } = loadImpl();
    const dir = mkTmp('load-config-missing-');
    let err;
    try {
      loadConfig(path.join(dir, 'does-not-exist.json'));
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'expected ConfigError');
    assert.equal(err.reason, 'bad-shape');
  });

  it('validates before any network or key access (works with no keys set)', () => {
    const { loadConfig } = loadImpl();
    assert.equal(process.env.MUSE_API_KEY, undefined);
    const bad = validConfig();
    bad.routes.reason.model = 'gpt-4';
    assert.throws(() => loadConfig(writeConfig(bad)), (e) => e.reason === 'non-muse-model');
  });
});
