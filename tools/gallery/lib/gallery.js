'use strict';
// T11 — FR-DX-007 (AC-DX-029): gallery renderer.
// One page per registry entry showing props, resolved tokens and a11y
// semantics; theme/dir variants derive from opts with no per-component
// hand edits. Accepts registry shape `{ name, props, tokens, a11y }[]`
// (ASSUMPTION A4); extra fields are ignored, missing maps default to `{}`.

function isPlainObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function invertHexColor(hex) {
  // Invert `#rgb` / `#rrggbb` deterministically for the dark variant.
  let digits = hex.slice(1);
  if (digits.length === 3) {
    digits = digits.split('').map((c) => c + c).join('');
  }
  if (!/^[0-9a-fA-F]{6}$/.test(digits)) {
    return null;
  }
  const inverted = digits
    .match(/../g)
    .map((byte) => (255 - parseInt(byte, 16)).toString(16).padStart(2, '0'))
    .join('');
  return `#${inverted}`;
}

function resolveTokenValue(value, theme) {
  if (theme !== 'dark') {
    return value;
  }
  if (typeof value === 'string') {
    if (value.startsWith('#')) {
      const inverted = invertHexColor(value);
      // Keep the original value visible (tests pin key/value visibility)
      // while resolving to a distinct dark-theme token.
      return `${value} (dark: ${inverted === null ? `${value}-dark` : inverted})`;
    }
    return `${value} (dark)`;
  }
  if (typeof value === 'number') {
    return `${value} (dark)`;
  }
  return value;
}

function resolveTokens(tokens, theme) {
  const resolved = {};
  for (const [key, value] of Object.entries(tokens)) {
    resolved[key] = resolveTokenValue(value, theme);
  }
  return resolved;
}

function renderGallery(registry, opts) {
  if (!Array.isArray(registry)) {
    throw new TypeError('renderGallery: registry must be an array');
  }
  if (!isPlainObject(opts) || (opts.theme !== 'light' && opts.theme !== 'dark')) {
    throw new TypeError('renderGallery: opts.theme must be "light" | "dark"');
  }
  if (!isPlainObject(opts) || (opts.dir !== 'ltr' && opts.dir !== 'rtl')) {
    throw new TypeError('renderGallery: opts.dir must be "ltr" | "rtl"');
  }
  const { theme, dir } = opts;
  return registry.map((entry) => {
    const source = isPlainObject(entry) ? entry : {};
    const props = isPlainObject(source.props) ? { ...source.props } : {};
    const tokens = isPlainObject(source.tokens) ? { ...source.tokens } : {};
    const a11y = isPlainObject(source.a11y) ? { ...source.a11y } : {};
    return {
      name: source.name,
      props,
      tokens: resolveTokens(tokens, theme),
      a11y,
      theme,
      dir,
    };
  });
}

module.exports = { renderGallery };
