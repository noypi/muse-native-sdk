'use strict';
// Generated template test — scaffolded with the app by `muse-native create`.
// Runnable via `node --test test/app.test.js` (see package.json "test" script).
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const appText = fs.readFileSync(path.join(__dirname, '..', 'src', 'App.tsx'), 'utf8');

describe('template app', () => {
  it('renders a view', () => {
    assert.match(appText, /<\w+[^>]*>/);
  });

  it('performs its network call', () => {
    assert.match(appText, /fetch\s*\(/);
  });

  it('wires navigation and theming', () => {
    assert.match(appText, /navigat/i);
    assert.match(appText, /theme/i);
  });
});
