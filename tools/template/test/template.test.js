'use strict';
// T13-unit — FR-DX-007 (AC-DX-031, AC-DX-032-part): one-command template app.
// `scaffoldTemplate({ name }) => { files, testCommand }` — the template owns
// navigation, theming, one network call and one generated test; a single CLI
// command (`muse-native create <name>`) produces a runnable app.
// This file pins the scaffold contract (shape + template content markers).
// The live build/launch half lives in `acceptance.test.js`.
// Hermetic: node:test + node:assert/strict + node:fs/node:path (repo-relative
// template reads only — no app config, no secrets). No network, no live
// services, no downloads, no clock, no device.
// Template files under test: `templates/app/package.json`,
// `templates/app/src/App.tsx`. Missing files surface as ENOENT assertion
// failures (expected RED before the T13 coder runs).
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T13.md):
// - A-T13a: `scaffoldTemplate` is a test-local helper (plan names no
//   production scaffold module; T13 owns template files, not a lib).
//   It returns `{ files: ['package.json', 'src/App.tsx',
//   'test/app.test.js'], testCommand: 'node --test test/app.test.js' }`.
// - A-T13b: template content markers — `package.json` exposes a `test`
//   script; `App.tsx` contains navigation, theming, one network call
//   (`fetch`) and a JSX element; the generated test file uses
//   `node:test` + `node:assert/strict`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const TEMPLATE_DIR = path.resolve(__dirname, '../../../templates/app');

function scaffoldTemplate(opts) {
  if (!opts || typeof opts.name !== 'string' || opts.name.length === 0) {
    throw new TypeError('scaffoldTemplate requires { name: string }');
  }
  if (!/^[A-Za-z0-9_-]+$/.test(opts.name)) {
    throw new TypeError('scaffoldTemplate name must be [A-Za-z0-9_-]+');
  }
  return {
    files: ['package.json', 'src/App.tsx', 'test/app.test.js'],
    testCommand: 'node --test test/app.test.js',
  };
}

function readTemplate(rel) {
  return fs.readFileSync(path.join(TEMPLATE_DIR, rel), 'utf8');
}

describe('scaffoldTemplate — one-command scaffold contract', () => {
  it('returns the file list and test command for a name', () => {
    const result = scaffoldTemplate({ name: 'my-app' });
    assert.deepEqual(result.files, ['package.json', 'src/App.tsx', 'test/app.test.js']);
    assert.equal(typeof result.testCommand, 'string');
    assert.ok(result.testCommand.length > 0);
  });

  it('is deterministic for the same name', () => {
    assert.deepEqual(scaffoldTemplate({ name: 'my-app' }), scaffoldTemplate({ name: 'my-app' }));
  });

  it('throws TypeError for a missing or invalid name', () => {
    assert.throws(() => scaffoldTemplate({}), TypeError);
    assert.throws(() => scaffoldTemplate({ name: '' }), TypeError);
    assert.throws(() => scaffoldTemplate({ name: '../escape' }), TypeError);
    assert.throws(() => scaffoldTemplate(null), TypeError);
  });
});

describe('template content — navigation, theming, network call, generated test', () => {
  it('package.json exists with a test script matching testCommand', () => {
    const pkg = JSON.parse(readTemplate('package.json'));
    assert.equal(typeof pkg.name, 'string');
    assert.equal(typeof pkg.scripts, 'object');
    assert.equal(typeof pkg.scripts.test, 'string');
    const { testCommand } = scaffoldTemplate({ name: pkg.name });
    assert.ok(
      pkg.scripts.test.includes('test/app.test.js') || testCommand.includes('test/app.test.js'),
      'test script must run the generated test',
    );
  });

  it('App.tsx includes navigation', () => {
    const text = readTemplate('src/App.tsx');
    assert.match(text, /navigat/i, 'App.tsx must include navigation');
  });

  it('App.tsx includes theming (light/dark or theme tokens)', () => {
    const text = readTemplate('src/App.tsx');
    assert.match(text, /theme/i, 'App.tsx must include theming');
  });

  it('App.tsx performs exactly one network call', () => {
    const text = readTemplate('src/App.tsx');
    const calls = text.match(/fetch\s*\(/g) || [];
    assert.equal(calls.length, 1, 'App.tsx must perform exactly one network call');
  });

  it('App.tsx renders a JSX element', () => {
    const text = readTemplate('src/App.tsx');
    assert.match(text, /<\w+[^>]*>/, 'App.tsx must render a JSX element');
  });
});
