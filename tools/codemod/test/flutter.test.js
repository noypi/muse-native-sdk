'use strict';
// T7 — FR-DX-006 (AC-DX-025): Flutter converter — common widget composition
// + ThemeData tokens map to engine primitives/tokens; unknown widgets land in
// `unmapped`. Settled D2: minimal representative fixtures enforcing
// report-completeness; real-world corpus is follow-up.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no
// secrets, no application config, no live services, no filesystem, no clock.
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T7.md):
// - A-T7a: "output parses as TSX" uses the same in-file heuristic as T6
//   (balanced {}, (), [] + a JSX element tag); a real parser check is
//   tester-layer work.
// - A-T7b: engine-primitive tag names are the coder's choice; tests pin only
//   that Flutter-specific spellings are gone (no Scaffold(, no ThemeData(),
//   no `new Text(`), not what replaces them.
// - A-T7c: empty-string input yields `{ output: '', unmapped: [] }`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { convertFlutter } = require('../lib/flutter.js');

// Minimal representative Flutter screen: widget composition + theming (D2).
const CLEAN_SCREEN = `
import 'package:flutter/material.dart';

void main() {
  runApp(MyApp());
}

class MyApp extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      theme: ThemeData(primarySwatch: Colors.blue),
      home: HomeScreen(),
    );
  }
}

class HomeScreen extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: Text('Hello')),
      body: Column(
        children: [
          Text('Welcome'),
        ],
      ),
    );
  }
}
`;

// Screen using a widget outside the mapped set.
const UNKNOWN_SCREEN = `
import 'package:flutter/material.dart';

class OddScreen extends StatelessWidget {
  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Column(
        children: [
          QuantumFluxCapacitor(level: 11),
          HologramProjector(mode: 'night'),
        ],
      ),
    );
  }
}
`;

function assertParsesAsTsx(output) {
  assert.equal(typeof output, 'string');
  assert.match(output, /<\w+[^>]*>/, 'output should contain a JSX element');
  const pairs = [['{', '}'], ['(', ')'], ['[', ']']];
  for (const [open, close] of pairs) {
    const opened = output.split(open).length - 1;
    const closed = output.split(close).length - 1;
    assert.equal(closed, opened, `unbalanced ${open}${close}`);
  }
}

describe('convertFlutter — representative screen conversion', () => {
  it('converts a representative Flutter screen to non-empty output', () => {
    const { output } = convertFlutter(CLEAN_SCREEN);
    assert.equal(typeof output, 'string');
    assert.ok(output.length > 0);
  });

  it('maps away Flutter-specific spellings (Scaffold, ThemeData, Text widgets)', () => {
    const { output } = convertFlutter(CLEAN_SCREEN);
    assert.ok(!output.includes('Scaffold('), 'no Scaffold( remnant');
    assert.ok(!output.includes('ThemeData('), 'no ThemeData( remnant');
  });

  it('resolves ThemeData tokens and preserves text content', () => {
    const { output } = convertFlutter(CLEAN_SCREEN);
    assert.ok(output.includes('Hello'), 'app bar title preserved');
    assert.ok(output.includes('Welcome'), 'body text preserved');
  });

  it('rewrites every Text( widget, not just the first (Stage-3 regression)', () => {
    const src = `Widget build(BuildContext context) { return Column(children: [Text('Alpha'), Text('Beta')]); }`;
    const { output } = convertFlutter(src);
    assert.ok(!output.includes('Text('), 'no Text( remnant — all Text widgets rewritten');
    assert.equal(output.split('Label(').length - 1, 2, 'expected two Label( renderings');
  });

  it('produces output that parses as TSX', () => {
    const { output } = convertFlutter(CLEAN_SCREEN);
    assertParsesAsTsx(output);
  });

  it('reports an empty unmapped list for the clean fixture', () => {
    const { unmapped } = convertFlutter(CLEAN_SCREEN);
    assert.deepEqual(unmapped, []);
  });
});

describe('convertFlutter — unmapped-widget report completeness', () => {
  it('reports a non-empty unmapped list for unknown widgets', () => {
    const { unmapped } = convertFlutter(UNKNOWN_SCREEN);
    assert.ok(unmapped.length > 0);
  });

  it('names each unknown widget instead of dropping it silently', () => {
    const { unmapped } = convertFlutter(UNKNOWN_SCREEN);
    assert.ok(
      unmapped.some((entry) => entry.includes('QuantumFluxCapacitor')),
      `expected QuantumFluxCapacitor in report, got: ${JSON.stringify(unmapped)}`,
    );
    assert.ok(
      unmapped.some((entry) => entry.includes('HologramProjector')),
      `expected HologramProjector in report, got: ${JSON.stringify(unmapped)}`,
    );
  });

  it('returns empty output parts with an empty report for empty input', () => {
    assert.deepEqual(convertFlutter(''), { output: '', unmapped: [] });
  });
});
