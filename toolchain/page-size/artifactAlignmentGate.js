'use strict';
// T3 — Final-artifact alignment gate over the shipped artifact set (AC-BUILD-018).
// Pure-data gate: entries are "<name>.so:<flag>" with flag "1" = 16 KB-aligned,
// "0" = misaligned. A misaligned .so anywhere blocks release.

function parseEntry(entry) {
  const text = String(entry);
  const sep = text.lastIndexOf(':');
  if (sep < 0) {
    return { name: text, aligned: false };
  }
  return { name: text.slice(0, sep), aligned: text.slice(sep + 1) === '1' };
}

function checkArtifactAlignment(artifact) {
  const input = artifact && typeof artifact === 'object' ? artifact : {};
  const violations = [];
  const baseApk = Array.isArray(input.baseApk) ? input.baseApk : [];
  for (const entry of baseApk) {
    const { name, aligned } = parseEntry(entry);
    if (!aligned) {
      violations.push(`base APK: ${name} is not 16 KB-aligned; release blocked`);
    }
  }
  const featureModules = Array.isArray(input.featureModules) ? input.featureModules : [];
  featureModules.forEach((moduleEntries, index) => {
    const entries = Array.isArray(moduleEntries) ? moduleEntries : [];
    for (const entry of entries) {
      const { name, aligned } = parseEntry(entry);
      if (!aligned) {
        violations.push(`feature module ${index}: ${name} is not 16 KB-aligned; release blocked`);
      }
    }
  });
  const addons = Array.isArray(input.addons) ? input.addons : [];
  addons.forEach((moduleEntries, index) => {
    const entries = Array.isArray(moduleEntries) ? moduleEntries : [];
    for (const entry of entries) {
      const { name, aligned } = parseEntry(entry);
      if (!aligned) {
        violations.push(`addon ${index}: ${name} is not 16 KB-aligned; release blocked`);
      }
    }
  });
  return { ok: violations.length === 0, violations };
}

module.exports = { checkArtifactAlignment };
