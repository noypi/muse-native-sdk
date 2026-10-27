'use strict';
// T2 — Registry publish alignment gate for native addons (AC-BUILD-017).
// Q-BUILD-001 vendor boundary: third-party prebuilts live outside engine
// control, so an unaligned vendor addon is rejected with a vendor-action
// message (ask the vendor to ship an aligned build), not silently stranded.

function checkRegistryAddon(addon) {
  const input = addon && typeof addon === 'object' ? addon : {};
  const name = typeof input.name === 'string' ? input.name : '';
  if (name.length === 0) {
    return { ok: false, reasons: ['<unnamed addon>: addon name is required; publish rejected'] };
  }
  if (input.segmentsAligned === true) {
    return { ok: true, reasons: [] };
  }
  const rawAbis = Array.isArray(input.abis) ? input.abis : [];
  const reasons = rawAbis.map((abi) => `${name}: ABI ${String(abi)} segments are not 16 KB-aligned; publish rejected`);
  if (reasons.length === 0) {
    reasons.push(`${name}: segments are not 16 KB-aligned; publish rejected`);
  }
  reasons.push(`${name}: third-party prebuilt is outside engine control; ask the vendor to ship a 16 KB-aligned build`);
  return { ok: false, reasons };
}

module.exports = { checkRegistryAddon };
