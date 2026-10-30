#!/usr/bin/env bash
# Sample Box2D package build + self-tests (FR-PKG-006/007, T8).
#
# Modes (plan T8 Interface line):
#   --verify                      pack manifest + JS entry + d.ts + per-ABI
#                                 .so into .mnpkg; exit 0 on pass, nonzero
#                                 naming the failure.
#   --self-test-missing-abi       drop one ABI lib from staging and prove the
#                                 coverage check fails naming that ABI
#                                 (nonzero exit naming the removed ABI).
#   --verify-no-graphics-symbols  scan staged libs + JS entry for EGL / GLES /
#                                 Vulkan / ANativeWindow symbols using the T7
#                                 pinned prefix pin ^(egl|gl|vk|vkCreate|
#                                 ANativeWindow); exit 0 when clean, nonzero
#                                 naming the symbol when present.
#
# The per-ABI .so files are build-time stubs standing in for the real Box2D
# static build (device-lab replaces them); verification is static — pack
# contents, JS surface shape, symbol scan — and never dlopens.
set -u

SAMPLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="$SAMPLE_DIR/muse-package.json"
ENTRY_JS="$SAMPLE_DIR/src/index.js"
ENTRY_DTS="$SAMPLE_DIR/src/index.d.ts"
OUT_DIR="$SAMPLE_DIR/build/out"
STAGE_DIR="$SAMPLE_DIR/build/stage"
MNPKG="$OUT_DIR/box2d-sample.mnpkg"

fail() {
  echo "build.sh: FAIL: $1" >&2
  exit 1
}

manifest_abis() {
  node -e 'console.log(JSON.parse(require("fs").readFileSync(process.argv[1], "utf8")).abis.join("\n"))' "$MANIFEST"
}

# Stage manifest + entry + d.ts + one stub .so per manifest ABI.
stage_all() {
  local abi
  rm -rf "$STAGE_DIR"
  mkdir -p "$STAGE_DIR" "$OUT_DIR"
  cp "$MANIFEST" "$STAGE_DIR/muse-package.json"
  mkdir -p "$STAGE_DIR/src"
  cp "$ENTRY_JS" "$STAGE_DIR/src/index.js"
  cp "$ENTRY_DTS" "$STAGE_DIR/src/index.d.ts"
  while IFS= read -r abi; do
    [ -n "$abi" ] || continue
    mkdir -p "$STAGE_DIR/native/$abi"
    # Build-time stub for the real Box2D per-ABI static lib (physics only).
    printf 'MUSE-BOX2D-PHYSICS-STUB abi=%s\n' "$abi" > "$STAGE_DIR/native/$abi/libgame-engine.so"
  done < <(manifest_abis)
}

# Every manifest ABI must have a staged lib; names the missing ABI.
check_abi_coverage() {
  local abi
  while IFS= read -r abi; do
    [ -n "$abi" ] || continue
    if [ ! -f "$STAGE_DIR/native/$abi/libgame-engine.so" ]; then
      echo "build.sh: FAIL: missing ABI lib for \"$abi\" (expected native/$abi/libgame-engine.so)" >&2
      return 1
    fi
  done < <(manifest_abis)
  return 0
}

# The JS surface must expose exactly createWorld/step/readBodies/destroy
# with arities 0/2/1/1, matching the manifest exports table.
check_js_surface() {
  node -e '
    const fs = require("fs");
    const manifest = JSON.parse(fs.readFileSync(process.argv[1], "utf8"));
    const entry = require(process.argv[2]);
    const expected = { createWorld: 0, step: 2, readBodies: 1, destroy: 1 };
    const keys = Object.keys(entry).sort();
    const want = Object.keys(expected).sort();
    if (JSON.stringify(keys) !== JSON.stringify(want)) {
      console.error(`build.sh: FAIL: JS surface must expose exactly ${want.join(",")}, got ${keys.join(",")}`);
      process.exit(1);
    }
    for (const [name, arity] of Object.entries(expected)) {
      if (typeof entry[name] !== "function" || entry[name].length !== arity) {
        console.error(`build.sh: FAIL: export "${name}" must be a function of arity ${arity}`);
        process.exit(1);
      }
    }
    const manifestNames = manifest.exports.map((e) => e.name).sort();
    if (JSON.stringify(manifestNames) !== JSON.stringify(want)) {
      console.error(`build.sh: FAIL: manifest exports must list exactly ${want.join(",")}`);
      process.exit(1);
    }
  ' "$MANIFEST" "$ENTRY_JS" || return 1
  return 0
}

# Scan staged libs + JS entry/d.ts for graphics symbols (T7 pinned pin).
check_no_graphics_symbols() {
  local hit
  hit=$(grep -rhoE '[A-Za-z_][A-Za-z0-9_]*' "$STAGE_DIR" "$ENTRY_JS" "$ENTRY_DTS" 2>/dev/null \
    | grep -E '^(egl|gl|vk|vkCreate|ANativeWindow)' | sort -u | head -n 5) || true
  if [ -n "$hit" ]; then
    echo "build.sh: FAIL: forbidden graphics symbol(s) found: $hit" >&2
    return 1
  fi
  return 0
}

pack_mnpkg() {
  rm -f "$MNPKG"
  (cd "$STAGE_DIR" && tar -cf "$MNPKG" muse-package.json src native) \
    || fail "could not pack $MNPKG"
  for member in muse-package.json src/index.js src/index.d.ts; do
    tar -tf "$MNPKG" | grep -qx "$member" \
      || fail ".mnpkg is missing \"$member\""
  done
  local abi
  while IFS= read -r abi; do
    [ -n "$abi" ] || continue
    tar -tf "$MNPKG" | grep -qx "native/$abi/libgame-engine.so" \
      || fail ".mnpkg is missing per-ABI lib for \"$abi\""
  done < <(manifest_abis)
}

do_verify() {
  [ -f "$MANIFEST" ] || fail "missing muse-package.json"
  [ -f "$ENTRY_JS" ] || fail "missing src/index.js"
  [ -f "$ENTRY_DTS" ] || fail "missing src/index.d.ts"
  stage_all
  check_abi_coverage || fail "ABI coverage check failed"
  check_js_surface || fail "JS surface check failed"
  check_no_graphics_symbols || fail "graphics-symbol scan failed"
  pack_mnpkg
  echo "build.sh: VERIFY OK: $MNPKG"
}

do_self_test_missing_abi() {
  stage_all
  local victim
  victim=$(manifest_abis | tail -n 1)
  [ -n "$victim" ] || fail "manifest declares no ABIs"
  rm -f "$STAGE_DIR/native/$victim/libgame-engine.so"
  if check_abi_coverage; then
    fail "self-test failed: coverage check passed with ABI \"$victim\" removed"
  fi
  echo "build.sh: SELF-TEST-MISSING-ABI OK: correctly refused load without ABI \"$victim\"" >&2
  return 0
}

do_verify_no_graphics_symbols() {
  stage_all
  if check_no_graphics_symbols; then
    echo "build.sh: NO-GRAPHICS-SYMBOLS OK"
    return 0
  fi
  return 1
}

case "${1:---verify}" in
  --verify) do_verify ;;
  --self-test-missing-abi) do_self_test_missing_abi ;;
  --verify-no-graphics-symbols) do_verify_no_graphics_symbols ;;
  *) echo "build.sh: unknown mode \"$1\" (want --verify | --self-test-missing-abi | --verify-no-graphics-symbols)" >&2; exit 2 ;;
esac
