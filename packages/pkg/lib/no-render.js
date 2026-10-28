'use strict';

// No-render rule (FR-PKG-007, AC-PKG-016).
// Refuses renderer-bound packages: graphics capabilities (`gpu-surface` /
// `window`, never grantable in MVP) and EGL / GLES / Vulkan / ANativeWindow
// imports. Pure string scan; never touches native libs / dlopen.

const { PkgError } = require('./errors.js');

// Graphics capabilities are never grantable in MVP — refusal only.
const FORBIDDEN_GRAPHICS_CAPABILITIES = ['gpu-surface', 'window'];

// Pinned forbidden-prefix list (plan T7 Detail, verbatim). Start-anchored and
// case-sensitive: mid-string occurrences do not match. Symbol strings are
// opaque — the fixed pattern tests the raw string, never an interpolated one.
const FORBIDDEN_SYMBOL_PATTERN = /^(egl|gl|vk|vkCreate|ANativeWindow)/;

function checkNoRender(manifest, importedSymbols) {
  const capabilities =
    manifest && Array.isArray(manifest.capabilities) ? manifest.capabilities : [];
  const symbols = Array.isArray(importedSymbols) ? importedSymbols : [];

  let forbiddenCapability = null;
  for (const capability of capabilities) {
    const name = String(capability);
    if (FORBIDDEN_GRAPHICS_CAPABILITIES.includes(name)) {
      forbiddenCapability = name;
      break;
    }
  }

  let forbiddenSymbol = null;
  for (const symbol of symbols) {
    const name = String(symbol);
    if (FORBIDDEN_SYMBOL_PATTERN.test(name)) {
      forbiddenSymbol = name;
      break;
    }
  }

  if (forbiddenCapability === null && forbiddenSymbol === null) {
    return;
  }

  // Single fail-closed throw names both the forbidden capability and the
  // forbidden symbol when both are present.
  if (forbiddenCapability !== null && forbiddenSymbol !== null) {
    throw new PkgError(
      'E_NO_RENDER',
      `capabilities.${forbiddenCapability}+symbols.${forbiddenSymbol}`,
      `capability "${forbiddenCapability}" and symbol "${forbiddenSymbol}" are forbidden: renderer-free packages must not declare graphics capabilities or import graphics symbols`,
    );
  }
  if (forbiddenCapability !== null) {
    throw new PkgError(
      'E_NO_RENDER',
      `capabilities.${forbiddenCapability}`,
      `capability "${forbiddenCapability}" is forbidden: renderer-free packages must not declare graphics capabilities`,
    );
  }
  throw new PkgError(
    'E_NO_RENDER',
    `symbols.${forbiddenSymbol}`,
    `symbol "${forbiddenSymbol}" is forbidden: renderer-free packages must not import graphics symbols`,
  );
}

module.exports = { checkNoRender, FORBIDDEN_GRAPHICS_CAPABILITIES, FORBIDDEN_SYMBOL_PATTERN };
