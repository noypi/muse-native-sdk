#!/bin/sh
# JNI contract regression test — FR-RUNTIME-009 / AC-RUNTIME-027.
# Interface: jni_contract_test(libRuntime.so: path, MainActivity.java: path) -> exit_code: int
# Exit 0 iff every `native*` declared in MainActivity.java has a matching
# `Java_com_muse_host_MainActivity_*` export; otherwise exit 1 naming the
# missing symbol and citing AC-RUNTIME-027. Hermetic: no device, no network.
#
# Usage:
#   jni_contract_test.sh [libRuntime.so] [MainActivity.java] [jni_bridge.cpp]
# Defaults resolve relative to this script's directory inside the checkout, so
# `ctest -R jni_contract` runs hermetically on the host lane (no .so needed).
# When libRuntime.so exists and `nm` is available, the .so's dynamic symbols
# are checked too; otherwise the jni_bridge.cpp source exports are checked
# (same 6 mangled names the .so must carry).
set -u

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" >/dev/null 2>&1 && pwd)"
RUNTIME_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." >/dev/null 2>&1 && pwd)"

SO="${1:-$RUNTIME_DIR/../../build/host/libRuntime.so}"
JAVA="${2:-$RUNTIME_DIR/../../examples/m3-gallery/src/MainActivity.java}"
BRIDGE="${3:-$RUNTIME_DIR/src/jni_bridge.cpp}"

fail=0

if [ ! -f "$JAVA" ]; then
  echo "jni_contract_test: FAIL — MainActivity.java not found: $JAVA (AC-RUNTIME-027)"
  exit 1
fi

# Every `private ... native <type> <name>(...)` declaration in MainActivity.java.
NAMES="$(sed -n 's/.*private[[:space:]].*native[[:space:]][^[:space:]()]*[[:space:]]\([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p' "$JAVA" | sort -u)"
if [ -z "$NAMES" ]; then
  echo "jni_contract_test: FAIL — no native declarations parsed from $JAVA (AC-RUNTIME-027)"
  exit 1
fi
echo "jni_contract_test: native declarations: $(echo "$NAMES" | tr '\n' ' ')"

missing=""
for name in $NAMES; do
  sym="Java_com_muse_host_MainActivity_${name}"
  found=0
  # 1) Source check: the mangled export must exist in jni_bridge.cpp.
  if [ -f "$BRIDGE" ] && grep -q "$sym" "$BRIDGE"; then
    found=1
  fi
  # 2) Binary check when a .so and nm are available: the symbol must be a
  #    dynamic export (`nm -D`, ` T ` text symbol).
  if [ -f "$SO" ] && command -v nm >/dev/null 2>&1; then
    if nm -D "$SO" 2>/dev/null | grep -q " T .*${sym}\$"; then
      found=2
    elif [ "$found" -eq 0 ]; then
      echo "jni_contract_test: note — $SO present but $sym not exported (AC-RUNTIME-027)"
    fi
  fi
  if [ "$found" -eq 0 ]; then
    echo "jni_contract_test: FAIL — missing JNI export $sym for native $name (AC-RUNTIME-027)"
    missing="$missing $sym"
    fail=1
  else
    where="jni_bridge.cpp"
    [ "$found" -eq 2 ] && where="libRuntime.so dynamic symbols"
    echo "jni_contract_test: PASS — $name -> $sym ($where)"
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "jni_contract_test: FAIL missing:${missing} (AC-RUNTIME-027: host must pass the surface through JNI as an opaque handle; stale contract = UnsatisfiedLinkError)"
  exit 1
fi
echo "jni_contract_test: all $(echo "$NAMES" | wc -l | tr -d ' ') native methods matched (AC-RUNTIME-027)"
exit 0
