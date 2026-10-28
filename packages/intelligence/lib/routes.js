'use strict';
// T2 — Proxy route availability check for transcribe/reason/generate-image.
// Consumes the FR-MUSE-002 contract only: verifies route descriptors plus
// `muse-native.yaml:muse.presets` key-union validation. Hermetic: `fetchImpl`
// is the only I/O and an `upstream:*` fetch must never fire.

const REQUIRED_ROUTES = ['transcribe', 'reason', 'generate-image'];

function err(code, extra) {
  return Object.assign({ code }, extra);
}

function isUpstreamUrl(url) {
  return String(url).startsWith('upstream:');
}

async function loadDoc(fetchImpl, name) {
  const res = await fetchImpl(name);
  if (res && typeof res.json === 'function') return res.json();
  return res;
}

function validateProps(props) {
  if (props == null) return;
  if (typeof props !== 'object') throw err('raw-param', { route: 'transcribe' });
  const forbiddenKeys = new Set(['model', 'modelid', 'endpoint', 'url', 'baseurl', 'apikey']);
  for (const [key, value] of Object.entries(props)) {
    const flat = String(key).toLowerCase().replace(/[_-]/g, '');
    if (forbiddenKeys.has(flat)) {
      if (flat === 'model' || flat === 'modelid') throw err('raw-model', { key });
      if (flat === 'endpoint' || flat === 'url' || flat === 'baseurl') {
        throw err('raw-endpoint', { key });
      }
      throw err('raw-param', { key });
    }
    if (typeof value === 'string') {
      if (/^muse-[a-z0-9-]+/i.test(value.trim())) throw err('raw-model', { key });
      if (/^https?:\/\//i.test(value.trim())) throw err('raw-endpoint', { key });
    }
  }
}

async function assertMuseRoutes(fetchImpl, options) {
  const opts = options ?? {};
  const { preset, props } = opts;

  // Reject raw model IDs / endpoints / params before any descriptor fetch,
  // hence certainly before any upstream call (which never happens here).
  validateProps(props);

  const routes = await loadDoc(fetchImpl, 'routes');
  const presets = await loadDoc(fetchImpl, 'presets');

  if (preset !== undefined && preset !== null) {
    const known = presets && typeof presets === 'object' ? Object.keys(presets) : [];
    if (!known.includes(preset)) {
      const bad = err('unknown-preset', { preset });
      throw bad;
    }
  }

  const missing = REQUIRED_ROUTES.filter((name) => !(routes && routes[name]));
  return { ok: missing.length === 0, missing };
}

module.exports = { assertMuseRoutes };
