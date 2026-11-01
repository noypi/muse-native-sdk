#!/bin/sh
# RULE-GRAPHICS-003 build gate (FR-GRAPHICS-015 T3): mock/oracle-painter
# reachability fails the gallery page target.
# Interface: sh engine/runtime/tests/gallery_page_no_oracle_check.sh <code-root>
# Exit 0 iff no mock/oracle-painter symbol (PaintCaption, oracle box painter)
# is reachable from the gallery page target link set; exit nonzero + naming
# the offending symbol otherwise. Hermetic: grep/nm over the page-target
# objects, mirroring the jni_contract_test.sh pattern. No device, no network.
#
# The gate resolves the gallery page target sources under
# <code-root>/engine/runtime (src/gallery_present.cpp plus the CMakeLists.txt
# page target). Reachability means a reference from inside the device-frame
# page entries (PresentRealGalleryPage, PresentDeviceGalleryPage bodies):
# oracle painters staying compiled elsewhere in the TU for
# gallery_present_test/golden tooling (RULE-GRAPHICS-003 test-only fence)
# do not fail the gate — only a call/reference from the page path does.
set -u

usage() {
  echo "Usage: sh engine/runtime/tests/gallery_page_no_oracle_check.sh <code-root>" >&2
}

if [ "$#" -lt 1 ] || [ -z "${1:-}" ]; then
  usage
  exit 2
fi

ROOT="$1"
if [ ! -d "$ROOT" ]; then
  echo "gallery_page_no_oracle_check: FAIL — code-root not found: $ROOT" >&2
  usage >&2
  exit 1
fi

RUNTIME_DIR="$ROOT/engine/runtime"
SRC="$RUNTIME_DIR/src/gallery_present.cpp"
CMAKE_LISTS="$RUNTIME_DIR/CMakeLists.txt"

if [ ! -f "$SRC" ]; then
  echo "gallery_page_no_oracle_check: FAIL — page target source not found: $SRC" >&2
  exit 1
fi
if [ ! -f "$CMAKE_LISTS" ]; then
  echo "gallery_page_no_oracle_check: FAIL — page target build file not found: $CMAKE_LISTS" >&2
  exit 1
fi

# Oracle-painter symbols fenced test-only by RULE-GRAPHICS-003. PaintCaption
# is the CPU caption painter; PaintOracleBox is the oracle box painter;
# RasterGalleryPage is the CPU oracle page entry. A reference from the page
# path to any of them fails the gate naming that symbol.
ORACLE_SYMBOLS="PaintCaption PaintOracleBox RasterGalleryPage"

TMPDIR="$(mktemp -d 2>/dev/null || echo "/tmp/gallery-no-oracle-$$")"
mkdir -p "$TMPDIR" 2>/dev/null
PAGE_BODY="$TMPDIR/page_body.txt"
trap 'rm -rf "$TMPDIR"' EXIT INT TERM

# Extract the device-frame page entry bodies: from each page-entry signature
# to its closing `}` at column 0. Covers PresentRealGalleryPage (T2
# component-record path) and every PresentDeviceGalleryPage overload when
# present; fixtures carry only PresentRealGalleryPage.
awk '
  /^bool PresentRealGalleryPage\(|^bool PresentDeviceGalleryPage\(/ { in_body = 1 }
  in_body { print }
  in_body && /^\}/ { in_body = 0 }
' "$SRC" > "$PAGE_BODY" 2>/dev/null

if [ ! -s "$PAGE_BODY" ]; then
  echo "gallery_page_no_oracle_check: FAIL — no gallery page entry (PresentRealGalleryPage) found in $SRC" >&2
  exit 1
fi

# Optional nm leg (mirrors jni_contract_test.sh binary-first shape): when a
# built page object exists, a defined/call-site oracle symbol there also
# fails the gate. Absence of objects never fails — the source gate decides.
if command -v nm >/dev/null 2>&1; then
  for obj in "$RUNTIME_DIR"/build/CMakeFiles/runtime.dir/src/gallery_present.cpp.o \
             "$ROOT"/engine/runtime/build/CMakeFiles/runtime.dir/src/gallery_present.cpp.o; do
    if [ -f "$obj" ]; then
      for sym in $ORACLE_SYMBOLS; do
        if nm "$obj" 2>/dev/null | grep -q " $sym"; then
          # Confirm reachability: the symbol must also be referenced from the
          # page entry body (a test-only definition linked elsewhere is fine).
          if grep -q "$sym" "$PAGE_BODY" 2>/dev/null; then
            echo "gallery_page_no_oracle_check: FAIL — oracle painter reachable from gallery page target: $sym ($obj)" >&2
            exit 1
          fi
        fi
      done
    fi
  done
fi

fail=0
for sym in $ORACLE_SYMBOLS; do
  if grep -q "$sym" "$PAGE_BODY" 2>/dev/null; then
    echo "gallery_page_no_oracle_check: FAIL — oracle painter reachable from gallery page target: $sym" >&2
    fail=1
  fi
done

if [ "$fail" -ne 0 ]; then
  exit 1
fi
echo "gallery_page_no_oracle_check: PASS — no oracle painter reachable from the gallery page target"
exit 0
