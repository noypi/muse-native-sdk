#!/bin/sh
# build-demos.sh — build/list the sample demos via the muse-native CLI.
#
# Usage: build-demos.sh [--list | --self-test-missing-package | <target>]
#   --list    print each demo build target, one per line.
#   --self-test-missing-package
#             sandboxed failure-path proof for AC-SAMPLE-021 (see below).
#   <target>  demo target to build (e.g. examples/m3-gallery,
#             examples/flappy). Bare demo names are accepted too:
#             `flappy` -> `examples/flappy`, `m3-gallery` ->
#             `examples/m3-gallery`; anything already starting with
#             `examples/` passes through unchanged, as does anything else
#             (the CLI reports unknown targets).
#
# Self-test mode (`--self-test-missing-package`, TC-SAMPLE-021): shadows
# `@muse/game-engine-sample` out of the local registry inside a temp
# sandbox ONLY (a mktemp dir holding a symlink farm of node_modules/@muse
# minus the package, plus a copy of the flappy sources — the real registry
# and the checkout are never mutated), rebuilds the sandboxed flappy copy,
# then resolves the demo's declared dependency through the Muse package
# loader (`packages/pkg/lib/loader.js`) against the shadow registry. That
# resolution must exit non-zero with stderr naming the package-loader error
# for `game-engine-sample` (a loader PkgError, not a generic bundler miss),
# proving the dependency is real rather than vendored. The trap removes the
# sandbox afterwards (restore). Prints PASS and exits 0 when the expectation
# holds, FAIL and exits 1 otherwise.
#
# Delegates to `muse-native build <target>` resolved via PATH and propagates
# the CLI exit code. Each target resolves only its own per-project
# `muse-native.yaml` (`<target>/muse-native.yaml`, no workspace-root
# fallback). No direct toolchain invocation lives here, except the loader
# probe inside the self-test, which exercises the loader directly to prove
# the failure surfaces at the package loader.
set -e -u
set -o pipefail 2>/dev/null || true

SCRIPT_DIR="$(dirname "$0")"

self_test_missing_package() {
  PKG_NAME="game-engine-sample"
  SCOPED="@muse/${PKG_NAME}"
  REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
  LOADER="${REPO_ROOT}/packages/pkg/lib/loader.js"
  FLAPPY_DIR="${REPO_ROOT}/examples/flappy"
  FLAPPY_MANIFEST="${FLAPPY_DIR}/package.json"

  if [ ! -f "$LOADER" ]; then
    echo "build-demos.sh: self-test: FAIL: package loader not found: $LOADER" >&2
    exit 1
  fi
  if ! command -v muse-native >/dev/null 2>&1; then
    echo "build-demos.sh: self-test: FAIL: muse-native not on PATH" >&2
    exit 1
  fi
  if ! command -v node >/dev/null 2>&1; then
    echo "build-demos.sh: self-test: FAIL: node not on PATH" >&2
    exit 1
  fi
  # The demo must genuinely depend on the package (not a vendored copy):
  # its source imports the scoped specifier (RULE-SAMPLE-001).
  if ! grep -q "$SCOPED" "${FLAPPY_DIR}/src/App.tsx" 2>/dev/null; then
    echo "build-demos.sh: self-test: FAIL: ${FLAPPY_DIR}/src/App.tsx does not import $SCOPED" >&2
    exit 1
  fi

  SANDBOX="$(mktemp -d)"
  cleanup() { rm -rf "$SANDBOX"; }
  trap cleanup EXIT INT TERM

  # Shadow registry: symlink farm of the local node_modules/@muse scope
  # MINUS game-engine-sample. Symlinks only read the real registry; nothing
  # outside $SANDBOX is created, moved, or removed.
  SHADOW_SCOPE="${SANDBOX}/node_modules/@muse"
  mkdir -p "$SHADOW_SCOPE"
  REAL_SCOPE="${REPO_ROOT}/node_modules/@muse"
  if [ -d "$REAL_SCOPE" ]; then
    for entry in "$REAL_SCOPE"/*; do
      [ -e "$entry" ] || continue
      base="$(basename "$entry")"
      if [ "$base" != "$PKG_NAME" ]; then
        ln -s "$entry" "${SHADOW_SCOPE}/${base}"
      fi
    done
  fi
  if [ -e "${SHADOW_SCOPE}/${PKG_NAME}" ]; then
    echo "build-demos.sh: self-test: FAIL: shadow registry still resolves $SCOPED" >&2
    exit 1
  fi

  # Rebuild flappy inside the sandbox: copy the demo sources (never the
  # registry) and build the copy, so no dist/ writes touch the checkout.
  SANDBOX_FLAPPY="${SANDBOX}/flappy"
  cp -r "$FLAPPY_DIR" "$SANDBOX_FLAPPY"
  rm -rf "${SANDBOX_FLAPPY}/dist"
  if ! BUILD_OUT="$(muse-native build "$SANDBOX_FLAPPY" 2>&1)"; then
    echo "build-demos.sh: self-test: FAIL: sandboxed flappy rebuild failed unexpectedly:" >&2
    echo "$BUILD_OUT" >&2
    exit 1
  fi

  # Loader probe: resolve the shadowed (absent) package through the Muse
  # package loader. Must fail non-zero with the loader's named error.
  SHADOW_PKG="${SHADOW_SCOPE}/${PKG_NAME}"
  PROBE_ERR="${SANDBOX}/probe.stderr"
  if LOADER_PATH="$LOADER" SHADOW_PKG="$SHADOW_PKG" node -e 'const { loadPackage } = require(process.env.LOADER_PATH); loadPackage(process.env.SHADOW_PKG);' 2>"$PROBE_ERR"; then
    echo "build-demos.sh: self-test: FAIL: package loader resolved $SCOPED from the shadow registry although it is absent" >&2
    exit 1
  fi
  if ! grep -q "$PKG_NAME" "$PROBE_ERR" 2>/dev/null; then
    echo "build-demos.sh: self-test: FAIL: loader stderr does not name $PKG_NAME:" >&2
    cat "$PROBE_ERR" >&2
    exit 1
  fi
  if ! grep -q "PkgError\|E_LAYOUT\|E_MANIFEST\|E_INTEGRITY" "$PROBE_ERR" 2>/dev/null; then
    echo "build-demos.sh: self-test: FAIL: stderr does not carry the package-loader error (generic miss?):" >&2
    cat "$PROBE_ERR" >&2
    exit 1
  fi

  echo "build-demos.sh: self-test: PASS: missing $SCOPED fails at the package loader:"
  grep "$PKG_NAME" "$PROBE_ERR" | head -1
  echo "build-demos.sh: self-test: sandbox removed; real registry untouched."
  exit 0
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  echo "Usage: build-demos.sh [--list | --self-test-missing-package | <target>]"
  exit 0
fi

if [ "${1:-}" = "--list" ]; then
  echo "examples/m3-gallery"
  echo "examples/flappy"
  exit 0
fi

if [ "${1:-}" = "--self-test-missing-package" ]; then
  self_test_missing_package
fi

if [ "$#" -ne 1 ] || [ -z "${1:-}" ]; then
  echo "Usage: build-demos.sh [--list | --self-test-missing-package | <target>]" >&2
  exit 2
fi

TARGET="$1"

# Bare demo names (TC-SAMPLE-019 invokes the bare form) map to their
# `examples/` target; `examples/` paths pass through unchanged.
case "$TARGET" in
  flappy) TARGET="examples/flappy" ;;
  m3-gallery) TARGET="examples/m3-gallery" ;;
esac

exec muse-native build "$TARGET"
