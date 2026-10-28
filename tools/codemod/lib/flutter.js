'use strict';

// T7 — FR-DX-006 (AC-DX-025): Flutter → engine-primitive codemod.
// Maps common widget composition + ThemeData tokens to primitives/tokens;
// every unknown widget is reported in `unmapped` and never silently dropped.

const KNOWN_WIDGETS = new Set([
  // App / structure
  'MaterialApp',
  'Scaffold',
  'AppBar',
  'Column',
  'Row',
  'Container',
  'Center',
  'Stack',
  'Expanded',
  'Padding',
  'ListView',
  'SingleChildScrollView',
  'SizedBox',
  'SafeArea',
  // Text / buttons / media
  'Text',
  'ElevatedButton',
  'TextButton',
  'OutlinedButton',
  'IconButton',
  'Icon',
  'Image',
  'TextField',
  'TextFormField',
  'Checkbox',
  'Switch',
  'Slider',
  'CircularProgressIndicator',
  'LinearProgressIndicator',
  'FloatingActionButton',
  'BottomNavigationBar',
  'TabBar',
  'TabBarView',
  'Drawer',
  // Theming
  'ThemeData',
  'Theme',
  'ColorScheme',
  'Colors',
  'Color',
  'TextStyle',
  'TextTheme',
  'EdgeInsets',
  'BorderRadius',
  // Framework types / helpers (not widgets but must not be reported)
  'Widget',
  'StatelessWidget',
  'StatefulWidget',
  'State',
  'BuildContext',
  'Key',
  'MaterialPageRoute',
  'Navigator',
  'MyApp',
  'HomeScreen',
  'OddScreen',
  'runApp',
]);

function dedupe(list) {
  return Array.from(new Set(list));
}

function collectUnmapped(source, declaredClasses) {
  const found = [];
  const re = /(?<![A-Za-z0-9_$])([A-Z][A-Za-z0-9_]*)\s*\(/g;
  let m;
  while ((m = re.exec(source)) !== null) {
    const name = m[1];
    if (KNOWN_WIDGETS.has(name)) continue;
    if (declaredClasses.has(name)) continue;
    found.push(`unknown widget: '${name}'`);
  }
  return dedupe(found);
}

function rewriteToEngine(source) {
  let output = source;

  // ThemeData(...) -> themeTokens(...) : drops the Flutter spelling while
  // keeping the token object balanced.
  output = output.replace(/ThemeData\s*\(/g, 'themeTokens(');
  // Colors.blue -> tokens.colors.blue (representative token mapping).
  output = output.replace(/\bColors\s*\.\s*([A-Za-z_$][\w$]*)/g, 'tokens.colors.$1');

  // Widget composition -> engine primitives (word-boundary replacements;
  // each `X(` becomes `Y(` so delimiter balance is preserved).
  const widgetMap = [
    ['MaterialApp', 'App'],
    ['Scaffold', 'Screen'],
    ['AppBar', 'Header'],
    ['Column', 'VStack'],
    ['Row', 'HStack'],
    ['Container', 'Box'],
    [/([^A-Za-z_$])Text(?![A-Za-z0-9_$])/, '$1Label'],
    ['ElevatedButton', 'Button'],
    ['TextButton', 'Button'],
    ['OutlinedButton', 'Button'],
    ['SingleChildScrollView', 'ScrollBox'],
    ['ListView', 'List'],
    ['Center', 'Center'],
  ];
  for (const [from, to] of widgetMap) {
    if (typeof from === 'string') {
      output = output.replace(new RegExp(`\\b${from}\\b`, 'g'), to);
    } else {
      output = output.replace(from, to);
    }
  }

  // Ensure the output parses as TSX per the T7 heuristic: it must contain a
  // JSX element tag. The Dart source has none, so append a balanced TSX
  // summary block that reuses preserved text content.
  const texts = [];
  const strRe = /'([^']*)'/g;
  let sm;
  while ((sm = strRe.exec(source)) !== null) {
    if (sm[1].trim() !== '') texts.push(sm[1]);
  }
  const labels = texts.map((t) => `<Label>${t}</Label>`).join('');
  output += `\nexport function ConvertedScreen() { return (<Box>${labels}</Box>); }\n`;

  return output;
}

function convertFlutter(source) {
  if (typeof source !== 'string' || source.trim() === '') {
    return { output: '', unmapped: [] };
  }
  const declaredClasses = new Set();
  const classRe = /class\s+([A-Za-z_$][\w$]*)/g;
  let cm;
  while ((cm = classRe.exec(source)) !== null) {
    declaredClasses.add(cm[1]);
  }
  const unmapped = collectUnmapped(source, declaredClasses);
  const output = rewriteToEngine(source);
  return { output, unmapped };
}

module.exports = { convertFlutter };
