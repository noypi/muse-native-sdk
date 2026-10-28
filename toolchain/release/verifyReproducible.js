'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const { normalizeReleaseArtifact, hashArtifact } = require('./reproducible.js');
const { parseSbom } = require('./sbom.js');
const { verifyProvenance } = require('./provenance.js');

function sha256Hex(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

function toPosixRel(rel) {
  return rel.split(path.sep).join('/');
}

// Returns { entries: Map(rel -> Buffer) | null, error: string | null }.
// Never throws: missing dirs become errors, not exceptions.
function readDirEntries(buildDir) {
  let stat = null;
  try {
    stat = fs.statSync(buildDir);
  } catch {
    return { entries: null, error: `build dir missing: '${buildDir}'` };
  }
  if (!stat.isDirectory()) {
    return { entries: null, error: `build dir not a directory: '${buildDir}'` };
  }
  const entries = new Map();
  const stack = [buildDir];
  try {
    while (stack.length > 0) {
      const current = stack.pop();
      const dirents = fs.readdirSync(current, { withFileTypes: true });
      for (const dirent of dirents) {
        const full = path.join(current, dirent.name);
        if (dirent.isDirectory()) {
          stack.push(full);
        } else if (dirent.isFile()) {
          const rel = toPosixRel(path.relative(buildDir, full));
          entries.set(rel, fs.readFileSync(full));
        }
      }
    }
  } catch (err) {
    return { entries: null, error: `cannot read build dir '${buildDir}': ${err.message}` };
  }
  return { entries, error: null };
}

function toArtifactEntries(map) {
  return [...map.entries()].map(([rel, bytes]) => ({ path: rel, bytes }));
}

function dirDigests(map) {
  return [...map.entries()].map(([rel, bytes]) => ({ path: rel, sha256: sha256Hex(bytes) }));
}

function verifyRelease(firstBuildDir, secondBuildDir, opts) {
  const failures = [];
  try {
    const first = readDirEntries(firstBuildDir);
    const second = readDirEntries(secondBuildDir);
    if (first.error) {
      failures.push(first.error);
    }
    if (second.error) {
      failures.push(second.error);
    }
    if (first.entries && second.entries) {
      // T1 hash-equivalent comparison over directory entries.
      const normA = normalizeReleaseArtifact(toArtifactEntries(first.entries));
      const normB = normalizeReleaseArtifact(toArtifactEntries(second.entries));
      if (hashArtifact(normA) !== hashArtifact(normB)) {
        const names = new Set([...first.entries.keys(), ...second.entries.keys()]);
        for (const name of [...names].sort()) {
          const a = first.entries.get(name);
          const b = second.entries.get(name);
          if (a === undefined) {
            failures.push(`file '${name}' missing from first build dir`);
          } else if (b === undefined) {
            failures.push(`file '${name}' missing from second build dir`);
          } else if (!a.equals(b)) {
            failures.push(`content mismatch for file '${name}': builds differ`);
          }
        }
        if (failures.length === 0) {
          failures.push('build dirs differ by hash with no per-file difference found');
        }
      }
    }

    // SBOM self-consistency via T2.
    const sbomPath = opts && opts.expectedSbomPath;
    if (typeof sbomPath !== 'string' || sbomPath.length === 0) {
      failures.push('missing expected SBOM path: opts.expectedSbomPath must be a non-empty string');
    } else {
      let sbomText = null;
      try {
        sbomText = fs.readFileSync(sbomPath, 'utf8');
      } catch {
        failures.push(`missing SBOM file: '${sbomPath}' cannot be read`);
      }
      if (sbomText !== null) {
        try {
          parseSbom(sbomText);
        } catch (err) {
          failures.push(`invalid SBOM file '${sbomPath}': ${err.message}`);
        }
      }
    }

    // Provenance binding via T3 against actual dir digests.
    const provPath = opts && opts.expectedProvenancePath;
    if (typeof provPath !== 'string' || provPath.length === 0) {
      failures.push(
        'missing expected provenance path: opts.expectedProvenancePath must be a non-empty string'
      );
    } else {
      let provText = null;
      try {
        provText = fs.readFileSync(provPath, 'utf8');
      } catch {
        failures.push(`missing provenance file: '${provPath}' cannot be read`);
      }
      if (provText !== null) {
        const dirsToCheck = [];
        if (first.entries) {
          dirsToCheck.push(first.entries);
        }
        if (second.entries) {
          dirsToCheck.push(second.entries);
        }
        if (dirsToCheck.length === 0) {
          const res = verifyProvenance(provText, []);
          if (!res.ok) {
            for (const msg of res.failures) {
              failures.push(msg);
            }
          }
        } else {
          const seen = new Set();
          for (const map of dirsToCheck) {
            const res = verifyProvenance(provText, dirDigests(map));
            if (!res.ok) {
              for (const msg of res.failures) {
                if (!seen.has(msg)) {
                  seen.add(msg);
                  failures.push(msg);
                }
              }
            }
          }
        }
      }
    }
  } catch (err) {
    failures.push(`verifyRelease failed: ${err && err.message ? err.message : String(err)}`);
  }
  if (failures.length > 0) {
    return { ok: false, failures };
  }
  return { ok: true, failures: [] };
}

module.exports = { verifyRelease };
