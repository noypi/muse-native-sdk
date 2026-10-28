'use strict';
// T12 — FR-DX-007 (AC-DX-030, AC-DX-032-part): pure generation over the
// registry input. Delegates to the T11 renderer; no hardcoded component
// list — adding a registry entry produces a new page with zero gallery edits.

const { renderGallery } = require('./gallery.js');

function galleryFromRegistry(registry) {
  if (!Array.isArray(registry)) {
    throw new TypeError('galleryFromRegistry: registry must be an array');
  }
  return renderGallery(registry, { theme: 'light', dir: 'ltr' });
}

module.exports = { galleryFromRegistry };
