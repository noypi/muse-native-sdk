#!/bin/sh
# release-sdk.sh — idempotent SDK release job; stamps build/VERSION from sdk.yaml.
#
# Usage: release-sdk.sh [--allow-dirty] [--check|--strict] [--help]
#   --allow-dirty   bypass the clean-tree gate (git status --porcelain).
#   --check         warn (exit 0) when engine bytes changed but sdk.yaml
#                   version did not.
#   --strict        fail (nonzero) on the same staleness signal.
#   --help          print usage and exit 0.
#
# On success exits 0 printing: version=<VER> sha=<sha256> sizes=<n bytes each>
# where sha is the hex sha256 of the written build/VERSION file.
# Failures exit nonzero naming the cause: SDK-VERSION-MISSING for a
# missing/bad sdk.yaml version, dirty tree without --allow-dirty, and
# SDK-ABI-MISSING for any missing/empty ABI lib.
set -e -u
set -o pipefail 2>/dev/null || true

ALLOW_DIRTY=0
MODE="run"
ABIS="arm64-v8a armeabi-v7a x86_64 x86"

usage() {
  echo "Usage: release-sdk.sh [--allow-dirty] [--check|--strict] [--help]"
  echo "  --allow-dirty   allow a dirty working tree"
  echo "  --check         warn (exit 0) if engine bytes changed without a version bump"
  echo "  --strict        fail (nonzero) if engine bytes changed without a version bump"
  echo "  --help          show this help"
}

# Resolve repo root from the script directory.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_YAML="$ROOT/sdk.yaml"
BUILD_DIR="$ROOT/build"
VERSION_FILE="$BUILD_DIR/VERSION"

while [ $# -gt 0 ]; do
  case "${1:-}" in
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    --check) MODE="check"; shift ;;
    --strict) MODE="strict"; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; usage >&2; exit 1 ;;
  esac
done

# --- sdk.yaml version gate (strict SemVer) ---
if [ ! -f "$SDK_YAML" ]; then
  echo "error: SDK-VERSION-MISSING: sdk.yaml not found at $SDK_YAML" >&2
  exit 1
fi
VER="$(grep -E '^version: [0-9]+\.[0-9]+\.[0-9]+$' "$SDK_YAML" 2>/dev/null | head -n 1 | sed -E 's/^version: //' || true)"

if [ -z "${VER:-}" ]; then
  echo "error: SDK-VERSION-MISSING: sdk.yaml has no 'version: X.Y.Z' line" >&2
  exit 1
fi

# --- 4-ABI ship gate: every lib must exist and be non-empty ---
SIZES=""
for abi in $ABIS; do
  LIB="$BUILD_DIR/android-$abi/libRuntime.so"
  if [ ! -s "$LIB" ]; then
    echo "error: SDK-ABI-MISSING: missing or empty ABI lib: build/android-$abi/libRuntime.so" >&2
    exit 1
  fi
  sz="$(wc -c < "$LIB" | tr -d ' \t\n')"
  if [ -z "$SIZES" ]; then
    SIZES="$sz"
  else
    SIZES="$SIZES $sz"
  fi
done

# --- clean-tree gate ---
if [ "$ALLOW_DIRTY" != "1" ]; then
  if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if [ -n "$(git -C "$ROOT" status --porcelain 2>/dev/null)" ]; then
      echo "error: dirty working tree (git status --porcelain non-empty); pass --allow-dirty to proceed" >&2
      exit 1
    fi
  fi
fi

# --- staleness signal (--check warns, --strict fails) ---
# Git-based: any tracked-file change versus HEAD while the sdk.yaml
# version is unchanged.
if [ "$MODE" = "check" ] || [ "$MODE" = "strict" ]; then
  if git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 \
    && git -C "$ROOT" rev-parse --verify HEAD >/dev/null 2>&1; then
    HEAD_VER="$(git -C "$ROOT" show HEAD:sdk.yaml 2>/dev/null | grep -E '^version: [0-9]+\.[0-9]+\.[0-9]+$' | head -n 1 | sed -E 's/^version: //' || true)"
    TRACKED_CHANGES="$(git -C "$ROOT" status --porcelain 2>/dev/null | grep -v '^??' | grep -v 'build/VERSION' || true)"
    if [ -n "${TRACKED_CHANGES:-}" ] && [ "${HEAD_VER:-}" = "$VER" ]; then
      if [ "$MODE" = "check" ]; then
        echo "warning: STALE: engine bytes changed but sdk.yaml version ($VER) did not" >&2
        echo "warning: STALE: engine bytes changed but sdk.yaml version ($VER) did not"
      else
        echo "error: STALE: engine bytes changed but sdk.yaml version ($VER) did not" >&2
        exit 1
      fi
    fi
  fi
fi

# --- stamp build/VERSION (atomic write) ---
mkdir -p "$BUILD_DIR"
DATE="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
# Content hash of the shipped engine bytes; embedded so the stamp is
# deterministic for a given tree (idempotent line 1 across re-runs).
CONTENT_SHA="$(cat "$BUILD_DIR"/android-*/libRuntime.so 2>/dev/null | sha256sum | cut -d' ' -f1)"
TMP="$VERSION_FILE.tmp"
{
  printf '%s\n' "$VER"
  printf 'sha=%s date=%s\n' "$CONTENT_SHA" "$DATE"
  printf 'abis=arm64-v8a armeabi-v7a x86_64 x86\n'
} > "$TMP"
# Ensure bytes reach disk before the atomic rename.
if command -v python3 >/dev/null 2>&1; then
  python3 -c 'import os,sys; fd=os.open(sys.argv[1], os.O_RDONLY); os.fsync(fd); os.close(fd)' "$TMP"
else
  sync 2>/dev/null || true
fi
mv "$TMP" "$VERSION_FILE"

# --- re-read + sha256sum proof ---
SHA="$(sha256sum "$VERSION_FILE" | cut -d' ' -f1)"
echo "version=$VER sha=$SHA sizes=$SIZES bytes each"
