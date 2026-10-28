'use strict';
// TC-PKG-016 (AC-PKG-016) — Batch 4 (T7 contract).
// Hermetic: node:test + node:assert/strict only. Inline manifests/symbol lists;
// no network, no secrets, no live services, no fs fixtures, no device/dlopen.
// Binds ONLY to the pinned T7 `Interface:` line, never to an implementation.
// Resolution: `../lib/no-render.js` once it lands, else the `../index.js`
// shim, so every behavior case below fails until T7 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let noRenderMod = {};
try {
  noRenderMod = require('../lib/no-render.js');
} catch {
  try {
    noRenderMod = require('../index.js');
  } catch {
    noRenderMod = {};
  }
}
const { checkNoRender } = noRenderMod;

function validManifest(overrides = {}) {
  return {
    name: 'acme-physics',
    version: '1.0.0',
    museApi: '1',
    entry: { form: 'prebuilt', main: 'src/index.js', types: 'src/index.d.ts' },
    abis: ['arm64-v8a'],
    engineAbi: 'deadbeef',
    exports: [],
    capabilities: [],
    ...overrides,
  };
}

function assertNoRenderError(fn, expectedCapabilityFragment, expectedSymbolFragment) {
  let err;
  try {
    fn();
  } catch (e) {
    err = e;
  }
  assert.ok(err, 'must throw a PkgError');
  assert.equal(err.code, 'E_NO_RENDER', `code must be E_NO_RENDER, got: ${err && err.code}`);
  assert.equal(typeof err.path, 'string', 'PkgError must carry a string path');
  assert.equal(typeof err.expected, 'string', 'PkgError must carry a string expected');
  if (expectedCapabilityFragment !== null) {
    assert.ok(
      String(err.path).includes(expectedCapabilityFragment) ||
        String(err.expected).includes(expectedCapabilityFragment),
      `error must name the forbidden capability "${expectedCapabilityFragment}", got path=${err.path} expected=${err.expected}`,
    );
  }
  if (expectedSymbolFragment !== null) {
    assert.ok(
      String(err.path).includes(expectedSymbolFragment) ||
        String(err.expected).includes(expectedSymbolFragment),
      `error must name the forbidden symbol "${expectedSymbolFragment}", got path=${err.path} expected=${err.expected}`,
    );
  }
  return err;
}

// ---------------------------------------------------------------------------
// checkNoRender — T7 contract:
// `checkNoRender(manifest: ValidatedManifest, importedSymbols: string[]): void`
// throws `PkgError{ code: "E_NO_RENDER", path: string, expected: string }`
// ---------------------------------------------------------------------------
describe('TC-PKG-016 — no-render rule (AC-PKG-016)', () => {
  it('passes a physics-only package: no graphics capabilities, no GL symbols', () => {
    const m = validManifest({ capabilities: [] });
    assert.doesNotThrow(() => checkNoRender(m, ['box2d_step', 'memcpy', 'malloc']));
  });

  it('passes with an empty symbol list', () => {
    assert.doesNotThrow(() => checkNoRender(validManifest(), []));
  });

  it('refuses the gpu-surface capability naming the capability', () => {
    const m = validManifest({ capabilities: ['gpu-surface'] });
    assertNoRenderError(() => checkNoRender(m, []), 'gpu-surface', null);
  });

  it('refuses the window capability naming the capability', () => {
    const m = validManifest({ capabilities: ['window'] });
    assertNoRenderError(() => checkNoRender(m, []), 'window', null);
  });

  it('refuses an EGL import naming the symbol', () => {
    assertNoRenderError(
      () => checkNoRender(validManifest(), ['eglGetDisplay']),
      null,
      'eglGetDisplay',
    );
  });

  it('refuses a GLES import naming the symbol', () => {
    assertNoRenderError(
      () => checkNoRender(validManifest(), ['glDrawArrays']),
      null,
      'glDrawArrays',
    );
  });

  it('refuses a Vulkan import naming the symbol', () => {
    assertNoRenderError(
      () => checkNoRender(validManifest(), ['vkCreateInstance']),
      null,
      'vkCreateInstance',
    );
  });

  it('refuses an ANativeWindow import naming the symbol', () => {
    assertNoRenderError(
      () => checkNoRender(validManifest(), ['ANativeWindow_acquire']),
      null,
      'ANativeWindow_acquire',
    );
  });

  it('names both capability and symbol when both are forbidden (ASSUMPTION: single error names both)', () => {
    const m = validManifest({ capabilities: ['gpu-surface'] });
    assertNoRenderError(
      () => checkNoRender(m, ['glClear']),
      'gpu-surface',
      'glClear',
    );
  });

  it('is prefix-anchored: a non-prefix occurrence does not match (ASSUMPTION: ^ anchor)', () => {
    assert.doesNotThrow(() => checkNoRender(validManifest(), ['myegllib_helper']));
  });

  it('does not confuse plain physics symbols with graphics prefixes', () => {
    assert.doesNotThrow(() =>
      checkNoRender(validManifest(), ['box2d_createWorld', 'step', 'readBodies']),
    );
  });

  it('treats symbol strings opaquely: metacharacters do not bypass the prefix scan (security)', () => {
    // Mid-string "gl" with shell metacharacters must not match (anchored ^);
    // a leading-prefix symbol with trailing metacharacters must still be refused.
    assert.doesNotThrow(() => checkNoRender(validManifest(), ['x;glClear']));
    assertNoRenderError(() => checkNoRender(validManifest(), ['glClear;evil']), null, 'glClear;evil');
  });

  it('refuses every forbidden prefix family at least once (egl/gl/vk/ANativeWindow)', () => {
    for (const sym of ['eglSwapBuffers', 'glBindTexture', 'vkQueueSubmit', 'ANativeWindow_release']) {
      assertNoRenderError(() => checkNoRender(validManifest(), [sym]), null, sym);
    }
  });
});
