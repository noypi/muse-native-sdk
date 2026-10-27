'use strict';
// T3 — release checklist generator (NFR-BUILD-009 / AC-BUILD-024, AC-BUILD-025).
// Renders the policy table (level, deadline, owning change) from the T1
// catalog so the checklist cannot drift.

const fs = require('node:fs');
const path = require('node:path');

function parseKey(catalogText, key) {
  const match = catalogText.match(new RegExp(`^\\s*${key}\\s*=\\s*"?([^"\\s#]+)"?\\s*$`, 'm'));
  return match ? match[1].trim() : null;
}

function loadPolicy() {
  try {
    const policyPath = path.join(__dirname, 'sdk-policy.json');
    return JSON.parse(fs.readFileSync(policyPath, 'utf8'));
  } catch (_) {
    return {};
  }
}

function generateReleaseChecklist(catalogPath, outPath) {
  const catalogText = fs.readFileSync(catalogPath, 'utf8');
  const targetSdk = parseKey(catalogText, 'targetSdk');
  if (targetSdk === null) {
    throw new Error(`catalog missing targetSdk: ${catalogPath}`);
  }
  const minSdk = parseKey(catalogText, 'minSdk');
  if (minSdk === null) {
    throw new Error(`catalog missing minSdk: ${catalogPath}`);
  }
  const ndkVersion = parseKey(catalogText, 'ndkVersion');
  if (ndkVersion === null) {
    throw new Error(`catalog missing ndkVersion: ${catalogPath}`);
  }

  const policy = loadPolicy();
  const deadline = policy.playDeadline || '2025-08-31';
  const owningChange = policy.owningChange || 'NFR-BUILD-009';

  const md =
    '# Release Checklist\n' +
    '\n' +
    'Generated from the version catalog so SDK pins cannot drift.\n' +
    '\n' +
    '## SDK pins\n' +
    '\n' +
    `- targetSdk: ${targetSdk}\n` +
    `- minSdk: ${minSdk}\n` +
    `- ndkVersion: ${ndkVersion}\n` +
    '\n' +
    '## Policy table\n' +
    '\n' +
    '| Level | Deadline | Owning change |\n' +
    '| --- | --- | --- |\n' +
    `| targetSdk ${targetSdk} | ${deadline} | ${owningChange} |\n` +
    `| minSdk ${minSdk} | ${deadline} | ${owningChange} |\n` +
    `| NDK ${ndkVersion} | ${deadline} | ${owningChange} |\n`;

  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  fs.writeFileSync(outPath, md);
}

module.exports = { generateReleaseChecklist };
