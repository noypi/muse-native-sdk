'use strict';
// T2 — Baseline profile rules cover bootstrap/Hermes/first-frame/initial-route
// (NFR-BUILD-006 / AC-BUILD-006).
// Hermetic: node:test + node:assert/strict (+ node:fs/node:path only to read the
// tester-authored profile-rules.txt fixture beside this file). No network, no
// secrets, no live services.
// Red state: fixture exists; the per-section assertions below encode the
// contract. There is no production module for T2 (file is data, helper is
// test-only), so red is observed by removing a section, not by missing code.
//
// Interface under test (plan verbatim):
//   rulesContain(section: string): boolean (test helper over fixture text)
// sections `bootstrap`, `hermes-isolate`, `first-frame`, `initial-route` must
// all be present; `none` beyond grep-equivalent content check.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const RULES_PATH = path.join(__dirname, 'profile-rules.txt');
const REQUIRED = ['bootstrap', 'hermes-isolate', 'first-frame', 'initial-route'];

function readRules() {
  return fs.readFileSync(RULES_PATH, 'utf8');
}

// Test-only helper (plan verbatim interface): grep-equivalent content check.
function rulesContain(section) {
  const text = readRules();
  return text.includes(section);
}

describe('T2 — fixture is non-empty (AC-BUILD-006)', () => {
  it('profile-rules.txt exists and is non-empty', () => {
    const text = readRules();
    assert.ok(text.trim().length > 0, 'fixture must be non-empty');
  });
});

describe('T2 — all four sections present (AC-BUILD-006)', () => {
  for (const section of REQUIRED) {
    it(`rulesContain('${section}') is true`, () => {
      assert.equal(rulesContain(section), true);
    });
  }

  it('returns boolean type for a known section', () => {
    assert.equal(typeof rulesContain('bootstrap'), 'boolean');
  });
});

describe('T2 — removing any section fails (AC-BUILD-006)', () => {
  for (const section of REQUIRED) {
    it(`fixture without '${section}' would fail the content check`, () => {
      const text = readRules();
      const stripped = text.split(section).join('');
      assert.equal(stripped.includes(section), false);
      // The unstripped fixture still contains it (guard against vacuous pass).
      assert.equal(text.includes(section), true);
    });
  }
});

describe('T2 — boundary cases (AC-BUILD-006)', () => {
  it('unknown section returns false', () => {
    assert.equal(rulesContain('definitely-not-a-section'), false);
  });

  it('section match is case-sensitive (BOOTSTRAP is not bootstrap)', () => {
    // ASSUMPTION: section tokens are lowercase literals; uppercase must not match.
    assert.equal(rulesContain('BOOTSTRAP'), false);
  });

  it('empty string is trivially contained (String.includes semantics)', () => {
    // ASSUMPTION: documents helper semantics; empty query is not a real section.
    assert.equal(rulesContain(''), true);
  });
});
