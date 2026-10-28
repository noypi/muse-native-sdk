'use strict';

// T6 — FR-DX-006 (AC-DX-024): React Native → engine-primitive codemod.
// Maps View/Text/StyleSheet/common stack-navigator shapes to engine
// primitives; every unmatched construct is appended to `untranslatable`
// and never silently dropped.

const KNOWN_MODULES = new Set([
  'react',
  'react-native',
  '@react-navigation/native',
  '@react-navigation/native-stack',
  '@react-navigation/stack',
]);

// JSX tags (or dotted paths) understood by the mapper, as they appear in
// the *input* source before rewriting.
const KNOWN_INPUT_TAGS = new Set([
  'View',
  'Text',
  'Image',
  'ScrollView',
  'FlatList',
  'TouchableOpacity',
  'Pressable',
  'Stack',
  'Navigator',
  'Screen',
  'Stack.Navigator',
  'Stack.Screen',
]);

function dedupe(list) {
  return Array.from(new Set(list));
}

function collectUntranslatable(source) {
  const untranslatable = [];

  // 1. Unknown imports: any module outside the known RN/navigation set.
  const importRe =
    /import\s*(?:[^'"]*?)\s*from\s*['"]([^'"]+)['"]/g;
  const namedRe = /import\s*\{([^}]*)\}\s*from\s*['"]([^'"]+)['"]/;
  let m;
  while ((m = importRe.exec(source)) !== null) {
    const mod = m[1];
    if (!KNOWN_MODULES.has(mod)) {
      untranslatable.push(`unknown import: '${mod}'`);
      const named = namedRe.exec(m[0]);
      if (named) {
        const members = named[1]
          .split(',')
          .map((s) => s.trim())
          .filter(Boolean);
        for (const member of members) {
          untranslatable.push(`unknown import: '${member}' from '${mod}'`);
        }
      } else {
        const defaultRe = /import\s+([A-Za-z_$][\w$]*)\s*from/.exec(m[0]);
        if (defaultRe) {
          untranslatable.push(`unknown import: '${defaultRe[1]}' from '${mod}'`);
        }
      }
    }
  }

  // 2. NativeModules / unknown bridge access.
  const nativeRe = /NativeModules\s*\.\s*([A-Za-z_$][\w$]*)/g;
  while ((m = nativeRe.exec(source)) !== null) {
    untranslatable.push(`unknown native module: 'NativeModules.${m[1]}'`);
  }

  // 3. Unknown JSX components: capitalised (or dotted) tags outside the
  //    known input set. Lowercase host tags are treated as known.
  const tagRe = /<\/?([A-Za-z][\w.]*)/g;
  while ((m = tagRe.exec(source)) !== null) {
    const tag = m[1];
    if (/^[a-z]/.test(tag)) continue;
    if (KNOWN_INPUT_TAGS.has(tag)) continue;
    const base = tag.split('.')[0];
    if (KNOWN_INPUT_TAGS.has(base)) continue;
    untranslatable.push(`unknown component: '<${tag}>'`);
  }

  return dedupe(untranslatable);
}

function rewriteToEngine(source) {
  let output = source;

  // StyleSheet.create({ ... }) -> ({ ... }): drops the StyleSheet spelling
  // while keeping the token object (and its values) in the output.
  output = output.replace(/StyleSheet\s*\.\s*create\s*\(/g, '(');

  // Imports -> engine primitives.
  output = output.replace(
    /import\s*\{[^}]*\}\s*from\s*['"]react-native['"];?/g,
    "import { Box, Label } from '@muse/engine';",
  );
  output = output.replace(
    /import\s*\{[^}]*\}\s*from\s*['"]@react-navigation\/[^'"]+['"];?/g,
    "import { Navigator, Screen } from '@muse/engine';",
  );

  // Stack-navigator shapes -> engine primitives.
  output = output.replace(/Stack\s*\.\s*Navigator/g, 'Navigator');
  output = output.replace(/Stack\s*\.\s*Screen/g, 'Screen');
  output = output.replace(/createNativeStackNavigator\s*\(\s*\)/g, 'createNavigator()');
  output = output.replace(/createStackNavigator\s*\(\s*\)/g, 'createNavigator()');

  // View/Text -> engine primitives (word-boundary so tokens like
  // `Interview` are untouched).
  output = output.replace(/\bView\b/g, 'Box');
  output = output.replace(/\bText\b/g, 'Label');

  return output;
}

function convertRn(source) {
  if (typeof source !== 'string' || source.trim() === '') {
    return { output: '', untranslatable: [] };
  }
  const untranslatable = collectUntranslatable(source);
  const output = rewriteToEngine(source);
  return { output, untranslatable };
}

module.exports = { convertRn };
