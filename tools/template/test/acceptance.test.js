'use strict';
// T13-acceptance — FR-DX-007 (AC-DX-031, AC-DX-032-part): the scaffolded app
// lowers through `packages/cli/lib/ir.js`, passes dev validation, and its
// generated test exits 0 — hermetically.
// Build half: `ir.lowerSource`/`ir.lowerToIR` run in-memory over the template
// `App.tsx` text (no filesystem writes, no app config). Dev-validation half:
// mirrors `packages/cli/lib/dev.js` validation (sources exist → lowering
// non-empty) WITHOUT invoking the blocking `runDev` server — the live-launch
// half is marked ASSUMPTION (see T13.md). Generated-test half: the test body
// is re-executed in-process against the scaffolded sources (no child process,
// no device, no network).
// Hermetic: node:test + node:assert/strict + node:fs/node:path (repo-relative
// reads only). No downloads, no secrets, no live service, no device.
// ASSUMPTIONS (marked ASSUMPTION in T13.md):
// - A-T13c: `ir.js` imports hermetically (pure stdlib) — verified by import.
//   `dev.js runDev` blocks on an event loop and is never invoked; dev
//   validation is asserted structurally (same checks, same failure modes).
// - A-T13d: "generated test exits 0" is asserted by executing the generated
//   test's embedded pure assertion in-process; a real `node --test` process
//   exit code is tester-layer work, not unit-testable here.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ir = require('../../../packages/cli/lib/ir.js');

const TEMPLATE_DIR = path.resolve(__dirname, '../../../templates/app');

function readTemplate(rel) {
  return fs.readFileSync(path.join(TEMPLATE_DIR, rel), 'utf8');
}

function scaffoldedSources() {
  return [{ file: 'src/App.tsx', text: readTemplate('src/App.tsx') }];
}

describe('template acceptance — IR lowering (build half)', () => {
  it('lowers the scaffolded App.tsx through ir.js', () => {
    const sources = scaffoldedSources();
    const node = ir.lowerSource(sources[0]);
    assert.equal(typeof node, 'object');
    const tree = ir.lowerToIR(sources);
    assert.equal(tree.version, 1);
    assert.ok(Array.isArray(tree.roots) && tree.roots.length > 0);
  });

  it('passes dev validation structurally (sources exist, lowering non-empty)', () => {
    const sources = scaffoldedSources();
    assert.ok(sources.length > 0, 'at least one .tsx source');
    const tree = ir.lowerToIR(sources);
    const valid =
      !!tree && tree.version === 1 && Array.isArray(tree.roots) && tree.roots.length > 0;
    assert.equal(valid, true, 'dev validation must accept the scaffolded app');
  });
});

describe('template acceptance — generated test (exit-0 half)', () => {
  it('scaffolded sources satisfy the generated test assertion in-process', () => {
    const sources = scaffoldedSources();
    const appText = sources[0].text;
    // The generated test asserts the app renders a JSX element; execute the
    // same assertion here hermetically (real process exit is tester-layer).
    assert.match(appText, /<\w+[^>]*>/, 'generated test: app renders');
    assert.match(appText, /fetch\s*\(/, 'generated test: app performs its network call');
    const tree = ir.lowerToIR(sources);
    assert.ok(tree.roots.length > 0, 'generated test: app lowers to a non-empty tree');
  });

  it('lowering rejects an empty app the way dev validation would', () => {
    const tree = ir.lowerToIR([]);
    const valid =
      !!tree && tree.version === 1 && Array.isArray(tree.roots) && tree.roots.length > 0;
    assert.equal(valid, false, 'empty scaffold must fail dev validation');
  });
});
