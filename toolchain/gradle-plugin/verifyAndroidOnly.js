'use strict';
// T1 — Android-only artifact floor (NFR-BUILD-001 / AC-BUILD-001).
// Scans build-graph inputs under rootDir for ABI/API declarations and
// iOS/Apple toolchain references. Every violation names the offending
// file (relative to rootDir) and the matched pattern.

const fs = require('node:fs');
const path = require('node:path');

const PATTERNS = ['xcode', 'xcrun', 'pbxproj', 'ios', 'darwin'];

function collectFiles(dir, out) {
  let entries;
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      collectFiles(full, out);
    } else if (entry.isFile()) {
      out.push(full);
    }
  }
  return out;
}

function verifyAndroidOnly(rootDir) {
  const violations = [];
  let files = [];
  try {
    const stat = fs.statSync(rootDir);
    if (!stat.isDirectory()) {
      return { ok: false, violations: [`${rootDir}: not a directory (expected build-input root)`] };
    }
    files = collectFiles(rootDir, []);
  } catch {
    return { ok: false, violations: [`${rootDir}: unreadable build-input root`] };
  }

  for (const full of files) {
    let content;
    try {
      content = fs.readFileSync(full, 'utf8');
    } catch {
      continue; // binary or unreadable — not a text build input
    }
    const rel = path.relative(rootDir, full);
    const lower = content.toLowerCase();
    for (const pattern of PATTERNS) {
      if (lower.includes(pattern)) {
        violations.push(`${rel}: found '${pattern}'`);
      }
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { verifyAndroidOnly };
