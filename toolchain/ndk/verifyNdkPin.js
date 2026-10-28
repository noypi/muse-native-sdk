'use strict';
// T1 — NDK r27 dual-ABI configure pin gate (NFR-RUNTIME-001 / AC-RUNTIME-025).
// Hermetic: node:assert/strict is not needed here; stdlib (fs/path) only.
// Never invokes cmake, never touches the network/secrets/env/live services.
// All real-config access goes through the injected <root>:
//   <root>/gradle/libs.versions.toml   version catalog carrying
//                                      ndkVersion = "27.2.12479018"
//   <root>/engine/runtime/cmake/android-ndk.cmake
//                                      Android configure include referencing
//                                      the pin and setting
//                                      ANDROID_PLATFORM=android-29
//
// cmakeConfigure(abi: string, rootDir?: string): int — abi is byte-exact
// `arm64-v8a` or `x86_64`; 0 on successful configure, non-zero otherwise.
// Every other ABI (including non-strings) returns non-zero without throwing.
// Any drift off the r27 pin, missing catalog, or include not referencing the
// pin fails closed (non-zero, never throws).

const fs = require('node:fs');
const path = require('node:path');

const NDK_PIN = '27.2.12479018';
const CATALOG_REL = path.join('gradle', 'libs.versions.toml');
const INCLUDE_REL = path.join('engine', 'runtime', 'cmake', 'android-ndk.cmake');

function defaultRoot() {
  return path.resolve(__dirname, '..', '..');
}

function readText(filePath) {
  try {
    return { text: fs.readFileSync(filePath, 'utf8') };
  } catch (err) {
    return { error: err };
  }
}

// cmakeConfigure(abi: string, rootDir?: string): int
function cmakeConfigure(abi, rootDir) {
  try {
    // Exact-match ABI gate first: no I/O, no throw, no folding/trimming.
    if (typeof abi !== 'string') return 1;
    if (abi !== 'arm64-v8a' && abi !== 'x86_64') return 1;

    const root = rootDir === undefined ? defaultRoot() : String(rootDir);
    let rootStat = null;
    try {
      rootStat = fs.statSync(root);
    } catch (err) {
      void err;
      rootStat = null;
    }
    if (!rootStat || !rootStat.isDirectory()) return 1;

    // Catalog must carry the exact r27 pin.
    const catalogRead = readText(path.join(root, CATALOG_REL));
    if (catalogRead.error) return 1;
    const pinPattern = new RegExp(
      'ndkVersion\\s*=\\s*"' + NDK_PIN.replace(/\./g, '\\.') + '"'
    );
    if (!pinPattern.test(catalogRead.text)) return 1;

    // Configure include must reference the pin and the API-29 platform.
    const includeRead = readText(path.join(root, INCLUDE_REL));
    if (includeRead.error) return 1;
    if (!includeRead.text.includes(NDK_PIN)) return 1;
    if (!/ANDROID_PLATFORM[\s=]+android-29/.test(includeRead.text)) return 1;

    return 0;
  } catch (err) {
    void err;
    return 1;
  }
}

module.exports = { cmakeConfigure, NDK_PIN };
