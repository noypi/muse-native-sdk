'use strict';

function auditLicenses(pkgs, denylist) {
  if (!Array.isArray(pkgs)) {
    throw new TypeError('auditLicenses: pkgs must be an array');
  }
  if (!Array.isArray(denylist)) {
    throw new TypeError('auditLicenses: denylist must be an array');
  }

  const denied = new Set();
  for (const entry of denylist) {
    if (typeof entry === 'string') {
      const norm = entry.trim().toLowerCase();
      if (norm.length > 0) {
        denied.add(norm);
      }
    }
  }

  const blocked = [];
  const packages = pkgs.map((p) => ({ ...(p !== null && p !== undefined && typeof p === 'object' ? p : {}) }));

  pkgs.forEach((p, i) => {
    const entry = p !== null && p !== undefined && typeof p === 'object' && !Array.isArray(p) ? p : {};
    const name = typeof entry.name === 'string' && entry.name.length > 0 ? entry.name : `packages[${i}]`;
    const license = typeof entry.license === 'string' ? entry.license.trim() : '';
    if (license.length === 0) {
      blocked.push(`${name}: missing or empty license`);
      return;
    }
    if (denied.has(license.toLowerCase())) {
      blocked.push(`${name}: disallowed license ${entry.license.trim()}`);
    }
  });

  const report = {
    generatedAt: new Date().toISOString(),
    packages,
    blocked: [...blocked],
  };

  return { ok: blocked.length === 0, blocked, report };
}

module.exports = { auditLicenses };
