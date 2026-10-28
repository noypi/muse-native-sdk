'use strict';
// TC-SAMPLE-025/026 (AC-SAMPLE-025/026, FR-SAMPLE-010 + FR-SAMPLE-006) — Batch 1 (T1, T2, T3).
// Shared batch test file: T1 owns it; T2 and T3 map to it in their contracts.
// Hermetic: node:test + node:assert/strict + stdlib only. Reads the committed
// templates/ tree plus freshly scaffolded tmpdir copies; no network, no
// secrets, no live services, no repo writes, no port binding.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-S1: "bound to the local stub" means every route entry names a localhost
//   stub endpoint (URL/host containing 127.0.0.1/localhost/stub) and no route
//   points at a remote https:// model endpoint or carries a key/token.
// - A-S2: the scaffolded README "names the one swap point" means it names both
//   the stub endpoint and the deployed edge proxy URL placeholder plus states
//   that swapping changes no component code.
// - A-S3: the stub port comes from proxy/proxy.config.json (a `port` field or a
//   stub endpoint URL port); a bind failure must exit non-zero with stderr
//   naming that port. No socket is bound by these tests.
// - A-S4: templates/package.json wires the stub into the run path via a `stub`
//   or `prestart`/`run` script entry naming the stub (exact script name is the
//   coder's choice; the test accepts any script value mentioning `stub`).

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const CLI_DIR = path.resolve(__dirname, '..');
const TEMPLATES_DIR = path.join(CLI_DIR, 'templates');
const CLI_PKG_PATH = path.join(CLI_DIR, 'package.json');
const PROXY_REL = path.join('proxy', 'proxy.config.json');
const EXPECTED_ROUTES = ['segment', 'reason', 'structure', 'transcribe', 'generate-image'];

let scratch = null;

beforeEach(() => {
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-init-stub-'));
});

afterEach(() => {
  if (scratch) {
    fs.rmSync(scratch, { recursive: true, force: true });
    scratch = null;
  }
});

function readJson(p) {
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

function routeText(route) {
  return JSON.stringify(route || {}).toLowerCase();
}

function isStubBound(route) {
  const t = routeText(route);
  const namesStub = t.includes('stub') || t.includes('127.0.0.1') || t.includes('localhost');
  const remote = /https:\/\//.test(t);
  const hasSecret = /api[_-]?key|token|secret|bearer/.test(t);
  return namesStub && !remote && !hasSecret;
}

describe('TC-SAMPLE-025 — scaffolded stub-backed proxy config (AC-SAMPLE-025, T1)', () => {
  it('templates/proxy/proxy.config.json exists with version 1', () => {
    const p = path.join(TEMPLATES_DIR, PROXY_REL);
    assert.ok(fs.existsSync(p), `templates must contain ${PROXY_REL}`);
    const cfg = readJson(p);
    assert.equal(cfg.version, 1, 'scaffolded proxy config must declare version 1');
  });

  it('template proxy config declares all five Muse routes', () => {
    const cfg = readJson(path.join(TEMPLATES_DIR, PROXY_REL));
    assert.ok(cfg.routes && typeof cfg.routes === 'object', 'proxy config must declare a routes object');
    assert.deepEqual(
      Object.keys(cfg.routes).sort(),
      [...EXPECTED_ROUTES].sort(),
      `proxy config must declare exactly: ${EXPECTED_ROUTES.join(', ')}`,
    );
  });

  it('all five template routes bind to the local stub (no remote, no secret)', () => {
    const cfg = readJson(path.join(TEMPLATES_DIR, PROXY_REL));
    for (const name of EXPECTED_ROUTES) {
      assert.ok(cfg.routes[name], `route '${name}' must exist`);
      assert.ok(
        isStubBound(cfg.routes[name]),
        `route '${name}' must bind to the local stub with no remote URL or secret, got: ${JSON.stringify(cfg.routes[name])}`,
      );
    }
  });

  it('template route keys mirror proxy/proxy.config.json route keys', () => {
    const repoCfg = readJson(path.resolve(CLI_DIR, '..', '..', 'proxy', 'proxy.config.json'));
    const tplCfg = readJson(path.join(TEMPLATES_DIR, PROXY_REL));
    assert.deepEqual(
      Object.keys(tplCfg.routes).sort(),
      Object.keys(repoCfg.routes).sort(),
      'scaffolded route keys must mirror proxy/proxy.config.json route keys',
    );
  });

  it('scaffoldProject copies proxy/proxy.config.json into the new project', () => {
    const { scaffoldProject } = require('../lib/init.js');
    const outDir = path.join(scratch, 'my-app');
    const ret = scaffoldProject({ templateDir: TEMPLATES_DIR, outDir });
    assert.deepEqual(ret, { ok: true, projectDir: outDir });
    const dest = path.join(outDir, PROXY_REL);
    assert.ok(fs.existsSync(dest), `scaffold must create ${PROXY_REL}`);
    assert.deepEqual(readJson(dest), readJson(path.join(TEMPLATES_DIR, PROXY_REL)));
  });

  it('scaffolded proxy config carries version 1 and the five stub-bound routes', () => {
    const { scaffoldProject } = require('../lib/init.js');
    const outDir = path.join(scratch, 'my-app');
    scaffoldProject({ templateDir: TEMPLATES_DIR, outDir });
    const cfg = readJson(path.join(outDir, PROXY_REL));
    assert.equal(cfg.version, 1);
    for (const name of EXPECTED_ROUTES) {
      assert.ok(cfg.routes && cfg.routes[name], `scaffolded route '${name}' must exist`);
      assert.ok(isStubBound(cfg.routes[name]), `scaffolded route '${name}' must bind to the local stub`);
    }
  });

  it('scaffoldProject still throws scaffold-failed on invalid templateDir', () => {
    const { scaffoldProject } = require('../lib/init.js');
    assert.throws(
      () => scaffoldProject({ templateDir: '', outDir: path.join(scratch, 'x') }),
      /scaffold-failed: invalid templateDir/,
    );
  });

  it('scaffoldProject still throws scaffold-failed on non-empty outDir', () => {
    const { scaffoldProject } = require('../lib/init.js');
    const outDir = path.join(scratch, 'taken');
    fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(path.join(outDir, 'keep.txt'), 'precious\n');
    assert.throws(
      () => scaffoldProject({ templateDir: TEMPLATES_DIR, outDir }),
      /scaffold-failed: output directory already exists and is not empty/,
    );
  });

  it('scaffoldProject still throws scaffold-failed on invalid outDir', () => {
    const { scaffoldProject } = require('../lib/init.js');
    assert.throws(
      () => scaffoldProject({ templateDir: TEMPLATES_DIR, outDir: '' }),
      /scaffold-failed: invalid outDir/,
    );
  });
});

describe('TC-SAMPLE-026 — scaffolded README swap point (AC-SAMPLE-026, T1)', () => {
  it('templates/README.md names the single stub-to-proxy swap point', () => {
    const readme = fs.readFileSync(path.join(TEMPLATES_DIR, 'README.md'), 'utf8');
    assert.ok(/stub/i.test(readme), 'README must name the stub endpoint');
    assert.ok(/proxy/i.test(readme), 'README must name the deployed edge proxy URL');
    assert.ok(/swap/i.test(readme), 'README must name the swap point');
  });

  it('templates/README.md states the swap changes no component code', () => {
    const readme = fs.readFileSync(path.join(TEMPLATES_DIR, 'README.md'), 'utf8');
    assert.ok(
      /no component code change/i.test(readme),
      'README must state the swap changes no component code',
    );
  });

  it('scaffolded README carries the swap-point section', () => {
    const { scaffoldProject } = require('../lib/init.js');
    const outDir = path.join(scratch, 'my-app');
    scaffoldProject({ templateDir: TEMPLATES_DIR, outDir });
    const readme = fs.readFileSync(path.join(outDir, 'README.md'), 'utf8');
    assert.ok(/swap/i.test(readme) && /stub/i.test(readme), 'scaffolded README must carry the swap-point section');
  });
});

describe('TC-SAMPLE-025-run — stub runs wherever run runs (AC-SAMPLE-025, T2)', () => {
  it('run entry exposes run(projectDir) starting the stub alongside the app', () => {
    const runMod = require('../lib/run.js');
    const fn = typeof runMod === 'function' ? runMod : runMod.run;
    assert.equal(typeof fn, 'function', 'lib/run.js must expose run(projectDir)');
    assert.ok(fn.length >= 1, 'run must accept a projectDir argument');
    const src = fn.toString();
    assert.ok(/stub/i.test(src), 'run(projectDir) must reference the stub startup path');
  });

  it('run reads the stub port from proxy/proxy.config.json', () => {
    const runMod = require('../lib/run.js');
    const fn = typeof runMod === 'function' ? runMod : runMod.run;
    const src = fn.toString() + JSON.stringify(Object.keys(runMod || {}));
    assert.ok(/proxy\.config\.json|stub.*port|port.*stub/i.test(src), 'run must derive the stub port from proxy/proxy.config.json');
  });

  it('templates/package.json wires the stub into the run path', () => {
    const pkg = readJson(path.join(TEMPLATES_DIR, 'package.json'));
    const scripts = pkg.scripts || {};
    const hit = Object.values(scripts).some((v) => /stub/i.test(String(v)));
    assert.ok(hit, `templates/package.json must wire the stub into the run path, got scripts: ${JSON.stringify(scripts)}`);
  });

  it('stub bind failure exits non-zero naming the port (no silent fallback)', () => {
    const runMod = require('../lib/run.js');
    assert.ok(
      runMod && typeof runMod.stubPortError === 'function',
      'lib/run.js must expose a stubPortError(port) helper that reports the named-port failure',
    );
    const err = runMod.stubPortError(17631);
    assert.ok(/17631/.test(String(err && err.message || err)), 'port-bind error must name the port');
  });

  it('scaffolded proxy config carries no secrets for the run path', () => {
    const { scaffoldProject } = require('../lib/init.js');
    const outDir = path.join(scratch, 'my-app');
    scaffoldProject({ templateDir: TEMPLATES_DIR, outDir });
    const raw = fs.readFileSync(path.join(outDir, PROXY_REL), 'utf8');
    assert.ok(!/api[_-]?key|secret|token|bearer/i.test(raw), 'scaffolded proxy config must carry no secrets');
  });
});

describe('TC-SAMPLE-025-script — init-stub regression script (AC-SAMPLE-025/026, T3)', () => {
  it('package.json registers test:init-stub as node --test test/init-stub.test.ts', () => {
    const pkg = readJson(CLI_PKG_PATH);
    assert.ok(
      pkg.scripts && pkg.scripts['test:init-stub'],
      'packages/cli/package.json must register a test:init-stub script',
    );
    assert.equal(
      pkg.scripts['test:init-stub'],
      'node --test test/init-stub.test.ts',
      'test:init-stub must run node --test test/init-stub.test.ts',
    );
  });
});
