'use strict';
// T6 — FR-DX-006 (AC-DX-024): React Native codemod — component trees,
// StyleSheet objects and common stack-navigator shapes map to engine
// primitives; every unmatched construct lands in `untranslatable`, never
// silently dropped. Settled D2: minimal representative fixtures enforcing
// report-completeness; real-world corpus is follow-up.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no
// secrets, no application config, no live services, no filesystem, no clock.
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T6.md):
// - A-T6a: "output parses as TSX" is asserted with an in-file heuristic:
//   balanced {}, (), [] delimiters plus at least one JSX element tag.
//   A real TSX parser check is tester-layer work, not unit-testable here.
// - A-T6b: engine-primitive tag names are the coder's choice; tests pin only
//   that RN-specific spellings are gone (no `react-native` import, no
//   StyleSheet.create, no <View>/<Text> JSX tags), not what replaces them.
// - A-T6c: empty-string input yields `{ output: '', untranslatable: [] }`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { convertRn } = require('../lib/rn.js');

// Minimal representative RN screen: View/Text tree + StyleSheet + a common
// stack-navigator graph (D2 fixture scope).
const CLEAN_SCREEN = `
import { View, Text, StyleSheet } from 'react-native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';

const Stack = createNativeStackNavigator();

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#ffffff' },
  title: { fontSize: 20, color: '#111111' },
});

function HomeScreen() {
  return (
    <View style={styles.container}>
      <Text style={styles.title}>Hello</Text>
    </View>
  );
}

export default function App() {
  return (
    <Stack.Navigator>
      <Stack.Screen name="Home" component={HomeScreen} />
    </Stack.Navigator>
  );
}
`;

// Screen using APIs outside the mapped set.
const UNKNOWN_SCREEN = `
import { View } from 'react-native';
import { CameraRig } from 'exotic-camera-sdk';

export default function ScanScreen() {
  NativeModules.SecretBridge.startScan();
  return (
    <View>
      <CameraRig mode="night" />
    </View>
  );
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

describe('convertRn — representative screen conversion', () => {
  it('converts a representative RN screen to non-empty output', () => {
    const { output } = convertRn(CLEAN_SCREEN);
    assert.equal(typeof output, 'string');
    assert.ok(output.length > 0);
  });

  it('maps away RN-specific spellings (import, StyleSheet, View/Text tags)', () => {
    const { output } = convertRn(CLEAN_SCREEN);
    assert.ok(!output.includes('react-native'), 'no react-native import remnant');
    assert.ok(!output.includes('StyleSheet'), 'no StyleSheet remnant');
    assert.ok(!/<View[ >]/.test(output), 'no <View> remnant');
    assert.ok(!/<Text[ >]/.test(output), 'no <Text> remnant');
  });

  it('resolves StyleSheet values into the output', () => {
    const { output } = convertRn(CLEAN_SCREEN);
    assert.ok(output.includes('#ffffff'), 'container background token resolved');
    assert.ok(output.includes('Hello'), 'text content preserved');
  });

  it('produces output that parses as TSX', () => {
    const { output } = convertRn(CLEAN_SCREEN);
    assertParsesAsTsx(output);
  });

  it('reports an empty untranslatable list for the clean fixture', () => {
    const { untranslatable } = convertRn(CLEAN_SCREEN);
    assert.deepEqual(untranslatable, []);
  });
});

describe('convertRn — untranslatable report completeness', () => {
  it('reports a non-empty untranslatable list for unknown APIs', () => {
    const { untranslatable } = convertRn(UNKNOWN_SCREEN);
    assert.ok(untranslatable.length > 0);
  });

  it('names each unknown construct instead of dropping it silently', () => {
    const { untranslatable } = convertRn(UNKNOWN_SCREEN);
    assert.ok(
      untranslatable.some((entry) => entry.includes('CameraRig')),
      `expected CameraRig in report, got: ${JSON.stringify(untranslatable)}`,
    );
    assert.ok(
      untranslatable.some((entry) => entry.includes('SecretBridge')),
      `expected SecretBridge in report, got: ${JSON.stringify(untranslatable)}`,
    );
  });

  it('returns empty output parts with an empty report for empty input', () => {
    assert.deepEqual(convertRn(''), { output: '', untranslatable: [] });
  });
});
