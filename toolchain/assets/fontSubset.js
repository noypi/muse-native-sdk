'use strict';
// T2 — Font subsetting gate (NFR-BUILD-017 / AC-BUILD-063, TC-BUILD-063).
// Hermetic node gate for the canonical `:toolchain:fontSubset` task; the
// native guard entry lives in `toolchain/font-subset/main.cpp`.
// Shipped locale set is consumed by reference (l10n spec); the dummy list
// below mirrors it for hermetic tests and is never redefined here.
// Fixture fonts are marker buffers: a font carrying a `CJK` marker stands in
// for a font containing unshipped (e.g. CJK unified-ideographs) scripts;
// real font parsing is out of scope for the hermetic gate (plan A4/A5).

// Dummy shipped set mirroring l10n by reference (latin-script locales used by
// the gate fixtures). Any requested locale outside this set is treated as an
// unshipped script (CJK stand-ins: 'ja', 'zh').
const SHIPPED_LOCALES = ['en', 'de', 'fr'];

class AssetError extends Error {
  constructor(code, message) {
    super(message || code);
    this.name = 'AssetError';
    this.code = code;
  }
}

function containsUnshippedScript(font) {
  // Marker fake (contract A2): `CJK` in the buffer means the font contains
  // scripts outside the shipped locale set.
  return font.indexOf('CJK') !== -1;
}

function subsetFont(font, locales) {
  if (!Buffer.isBuffer(font)) {
    throw new AssetError('invalid-font', 'font must be a Buffer');
  }
  if (!Array.isArray(locales)) {
    throw new AssetError('invalid-locales', 'locales must be an array');
  }
  // Fail closed on unshipped-script requests before touching font bytes.
  for (const locale of locales) {
    if (!SHIPPED_LOCALES.includes(locale)) {
      throw new AssetError(
        'unshipped-script',
        `locale '${locale}' is outside the shipped locale set`
      );
    }
  }
  // Audit: a font containing unshipped scripts fails even when the request
  // itself names only shipped locales (AC-BUILD-063).
  if (containsUnshippedScript(font)) {
    throw new AssetError(
      'unshipped-script',
      'font contains scripts outside the shipped locale set'
    );
  }
  // Subset to the requested locales: coverage is exactly the request (hence
  // always a subset of it), bytes are the surviving font payload.
  const coverage = locales.slice();
  const bytes = Buffer.from(font);
  return { bytes, coverage };
}

module.exports = { subsetFont, AssetError, SHIPPED_LOCALES };
