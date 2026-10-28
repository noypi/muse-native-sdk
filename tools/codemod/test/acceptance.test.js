'use strict';
// T10 — FR-DX-006 (AC-DX-028): RN + Flutter representative-screen acceptance
// conversion. Runs T6/T7 over the two committed fixtures; asserts the output
// renders (parses as TSX) and every untranslated construct appears in the
// report. Settled D2: minimal-but-representative fixtures enforcing
// report-completeness; real-world corpus is follow-up.
// Hermetic: node:test + node:assert/strict + stdlib only. Reads only the two
// committed fixture files beside this test. No network, no secrets, no
// application config, no live services, no clock.
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T10.md):
// - A-T10a: `convertAndRender` is defined in this test file (the plan names
//   no production module for it; T10 owns no lib file). It wraps the T6/T7
//   converters and the shared TSX-parse heuristic.
// - A-T10b: "renders" uses the same in-file heuristic as T6/T7 (balanced {},
//   (), [] delimiters plus at least one JSX element tag); a real parser or
//   device-render check is tester-layer work.
// - A-T10c: unknown `kind` throws a TypeError; empty-string source yields
//   `{ renders: false, report: [] }`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { convertRn } = require('../lib/rn.js');
const { convertFlutter } = require('../lib/flutter.js');

function parsesAsTsx(output) {
  if (typeof output !== 'string' || output.length === 0) return false;
  if (!/<\w+[^>]*>/.test(output)) return false;
  const pairs = [['{', '}'], ['(', ')'], ['[', ']']];
  for (const [open, close] of pairs) {
    const opened = output.split(open).length - 1;
    const closed = output.split(close).length - 1;
    if (closed !== opened) return false;
  }
  return true;
}

function convertAndRender(kind, source) {
  if (kind === 'rn') {
    const { output, untranslatable } = convertRn(source);
    return { renders: parsesAsTsx(output), report: untranslatable };
  }
  if (kind === 'flutter') {
    const { output, unmapped } = convertFlutter(source);
    return { renders: parsesAsTsx(output), report: unmapped };
  }
  throw new TypeError(`unknown kind: '${kind}' (expected "rn" | "flutter")`);
}

const RN_SCREEN = fs.readFileSync(
  path.join(__dirname, '..', 'fixtures', 'rn-screen.jsx'),
  'utf8',
);
const FLUTTER_SCREEN = fs.readFileSync(
  path.join(__dirname, '..', 'fixtures', 'flutter-screen.dart'),
  'utf8',
);

// Every untranslated construct deliberately placed in the RN fixture (D2:
// report-completeness — each must appear in the report, none silently
// dropped).
const RN_EXPECTED = ['CameraRig', 'SecretBridge', 'exotic-camera-sdk'];
// Every unmapped widget deliberately placed in the Flutter fixture.
const FLUTTER_EXPECTED = ['QuantumFluxCapacitor', 'HologramProjector'];

describe('convertAndRender — RN representative-screen acceptance', () => {
  it('converts the RN fixture to output that renders', () => {
    const { renders } = convertAndRender('rn', RN_SCREEN);
    assert.equal(renders, true);
  });

  it('lists every untranslated RN construct in the report', () => {
    const { report } = convertAndRender('rn', RN_SCREEN);
    assert.ok(Array.isArray(report));
    for (const name of RN_EXPECTED) {
      assert.ok(
        report.some((entry) => entry.includes(name)),
        `expected '${name}' in report, got: ${JSON.stringify(report)}`,
      );
    }
  });

  it('reports a non-empty report for the RN fixture', () => {
    const { report } = convertAndRender('rn', RN_SCREEN);
    assert.ok(report.length >= RN_EXPECTED.length);
  });
});

describe('convertAndRender — Flutter representative-screen acceptance', () => {
  it('converts the Flutter fixture to output that renders', () => {
    const { renders } = convertAndRender('flutter', FLUTTER_SCREEN);
    assert.equal(renders, true);
  });

  it('lists every unmapped Flutter widget in the report', () => {
    const { report } = convertAndRender('flutter', FLUTTER_SCREEN);
    assert.ok(Array.isArray(report));
    for (const name of FLUTTER_EXPECTED) {
      assert.ok(
        report.some((entry) => entry.includes(name)),
        `expected '${name}' in report, got: ${JSON.stringify(report)}`,
      );
    }
  });

  it('reports a non-empty report for the Flutter fixture', () => {
    const { report } = convertAndRender('flutter', FLUTTER_SCREEN);
    assert.ok(report.length >= FLUTTER_EXPECTED.length);
  });
});

describe('convertAndRender — boundary and error paths', () => {
  it('throws a TypeError for an unknown kind', () => {
    assert.throws(() => convertAndRender('xml', RN_SCREEN), TypeError);
  });

  it('returns renders:false with an empty report for empty RN input', () => {
    assert.deepEqual(convertAndRender('rn', ''), { renders: false, report: [] });
  });

  it('returns renders:false with an empty report for empty Flutter input', () => {
    assert.deepEqual(convertAndRender('flutter', ''), {
      renders: false,
      report: [],
    });
  });
});
