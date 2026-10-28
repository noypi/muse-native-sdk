'use strict';

function labelFor(entry, index) {
  if (entry !== null && typeof entry === 'object' && !Array.isArray(entry)) {
    if (typeof entry.name === 'string' && entry.name.length > 0) {
      return entry.name;
    }
  }
  return `addons[${index}]`;
}

function hasAbi(entry) {
  return typeof entry.abi === 'string' && entry.abi.trim().length > 0;
}

function verifyNativeAddons(addons) {
  if (!Array.isArray(addons)) {
    throw new TypeError('verifyNativeAddons: addons must be an array');
  }
  const violations = [];
  for (let i = 0; i < addons.length; i += 1) {
    const entry = addons[i];
    const label = labelFor(entry, i);
    if (entry === null || entry === undefined || typeof entry !== 'object' || Array.isArray(entry)) {
      violations.push(`${label}: entry is not an object`);
      continue;
    }
    if (!hasAbi(entry)) {
      violations.push(`${label}: missing ABI tag`);
    }
    if (entry.prebuiltSoPerAbi !== true) {
      violations.push(`${label}: missing per-ABI prebuilt .so`);
    }
    if (entry.elfAligned16k !== true) {
      violations.push(`${label}: ELF output is not 16k-aligned`);
    }
  }
  return { ok: violations.length === 0, violations };
}

module.exports = { verifyNativeAddons };
