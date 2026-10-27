'use strict';
// T5b — native-release invariant gate (NFR-BUILD-016 / AC-BUILD-061).
// Asserts a release tree exports only declared plugin-ABI symbols, ships a
// stripped .so, retains unstripped artifacts, and keeps the render path
// exception-free. Violations name the file and the offending flag/symbol.
//
// Release-tree layout (pinned by the unit-test contract):
//   <root>/flags.txt                  expected-flags source (mirrors T4's
//                                    toolchain/ndk/release-flags.txt)
//   <root>/render-flags.txt           render-path TU flags (exception-free)
//   <root>/abi-allowlist.txt          declared plugin-ABI symbols, one per line
//   <root>/exported-symbols.txt       actual release exports, one per line
//   <root>/lib/<abi>/libmuse.so       shipped stripped marker (text "STRIPPED")
//   <root>/symbols/unstripped/<abi>/libmuse.so  retained unstripped marker
//                                    (text "UNSTRIPPED")

const fs = require('node:fs');
const path = require('node:path');

const SONAME = 'libmuse.so';
const RENDER_FLAGS_FILE = 'render-flags.txt';

const ENABLING_TOKENS = new Set([
  '-fexceptions',
  '-fobjc-exceptions',
  '-fcxx-exceptions',
  '-fseh-exceptions',
]);

function stripComments(text) {
  return text
    .split('\n')
    .map((line) => {
      const hash = line.indexOf('#');
      return hash === -1 ? line : line.slice(0, hash);
    })
    .join('\n');
}

function scanEnablingTokens(text) {
  const found = [];
  for (const raw of stripComments(text).split(/\s+/)) {
    const token = raw.trim();
    if (!token) continue;
    if (ENABLING_TOKENS.has(token.toLowerCase())) found.push(token);
  }
  return found;
}

function parseSymbols(text) {
  return stripComments(text)
    .split('\n')
    .map((line) => line.trim())
    .filter((line) => line.length > 0);
}

function readText(filePath) {
  try {
    return { text: fs.readFileSync(filePath, 'utf8') };
  } catch (err) {
    return { error: err };
  }
}

function listAbis(dir) {
  try {
    return fs
      .readdirSync(dir, { withFileTypes: true })
      .filter((entry) => entry.isDirectory())
      .map((entry) => entry.name);
  } catch (err) {
    void err;
    return [];
  }
}

// verifyNativeRelease(rootDir: string): { ok: boolean, violations: string[] }
function verifyNativeRelease(rootDir) {
  const violations = [];
  const root = String(rootDir);

  let rootStat = null;
  try {
    rootStat = fs.statSync(root);
  } catch (err) {
    void err;
    rootStat = null;
  }
  if (!rootStat || !rootStat.isDirectory()) {
    return {
      ok: false,
      violations: [`${root}: release root not found or not a directory`],
    };
  }

  // 1. Render path must be exception-free (TC-BUILD-059 intent).
  const renderPath = path.join(root, RENDER_FLAGS_FILE);
  const renderRead = readText(renderPath);
  if (renderRead.error) {
    violations.push(
      `${RENDER_FLAGS_FILE}: cannot read render-path flags file '${renderPath}'`
    );
  } else {
    for (const token of scanEnablingTokens(renderRead.text)) {
      violations.push(
        `${RENDER_FLAGS_FILE}: render-path enables exceptions via '${token}'`
      );
    }
  }

  // 2. Release exports must stay within the declared plugin ABI (TC-BUILD-061).
  const allowlistPath = path.join(root, 'abi-allowlist.txt');
  const exportsPath = path.join(root, 'exported-symbols.txt');
  const allowlistRead = readText(allowlistPath);
  const exportsRead = readText(exportsPath);
  if (allowlistRead.error) {
    violations.push(
      `abi-allowlist.txt: cannot read declared plugin-ABI allowlist '${allowlistPath}'`
    );
  }
  if (exportsRead.error) {
    violations.push(
      `exported-symbols.txt: cannot read actual release exports '${exportsPath}'`
    );
  }
  if (!allowlistRead.error && !exportsRead.error) {
    const allowed = new Set(parseSymbols(allowlistRead.text));
    for (const symbol of parseSymbols(exportsRead.text)) {
      if (!allowed.has(symbol)) {
        violations.push(
          `exported-symbols.txt: unexpected export '${symbol}' outside the declared plugin ABI`
        );
      }
    }
  }

  // 3. A release ships a stripped .so per ABI.
  const libDir = path.join(root, 'lib');
  const shippedAbis = listAbis(libDir).filter((abi) =>
    fs.existsSync(path.join(libDir, abi, SONAME))
  );
  if (shippedAbis.length === 0) {
    violations.push(
      `lib/<abi>/${SONAME}: no shipped stripped artifact '${path.join(libDir, '<abi>', SONAME)}'`
    );
  } else {
    for (const abi of shippedAbis) {
      const shipped = path.join(libDir, abi, SONAME);
      const content = readText(shipped);
      if (content.error || !/STRIPPED/.test(content.text)) {
        violations.push(
          `lib/${abi}/${SONAME}: shipped artifact '${shipped}' is not a stripped release binary`
        );
      }
    }
  }

  // 4. Every shipped ABI retains its unstripped counterpart (TC-BUILD-061).
  const abisToCheck =
    shippedAbis.length > 0 ? shippedAbis : listAbis(path.join(root, 'symbols', 'unstripped'));
  if (shippedAbis.length > 0) {
    for (const abi of abisToCheck) {
      const unstripped = path.join(root, 'symbols', 'unstripped', abi, SONAME);
      const content = readText(unstripped);
      if (content.error || !/UNSTRIPPED/.test(content.text)) {
        violations.push(
          `symbols/unstripped/${abi}/${SONAME}: missing retained unstripped artifact for shipped '${SONAME}' (${abi})`
        );
      }
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { verifyNativeRelease };
