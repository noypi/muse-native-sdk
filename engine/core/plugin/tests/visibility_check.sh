#!/bin/sh
# T6 verification harness (AC-CORE-010): hidden-visibility check.
#
# Verifies the benchmark plugin built with hidden visibility exposes only
# its declared symbol set, using `llvm-nm --defined-only` allowlist diff.
# Fails closed with "llvm-nm not found" when the tool is absent (plan A3);
# never passes silently.
#
# Executed inside `./gradlew :engine:core:pluginAbi` (TC-CORE-010).
# Test-only surface exercising the T3/T4/T5 contracts; exposes no new
# production API.
set -eu

# Allowlist: the only symbols a hidden-visibility plugin build may define
# and export. MusePluginEntry is the single exported entry point (T3).
ALLOWLIST="MusePluginEntry"

PLUGIN_SO="${1:-build/benchmark_plugin.so}"

if ! command -v llvm-nm >/dev/null 2>&1; then
  echo "FAIL: llvm-nm not found (required for hidden-visibility check)" >&2
  exit 1
fi

if [ ! -f "$PLUGIN_SO" ]; then
  echo "FAIL: plugin object not found: $PLUGIN_SO" >&2
  exit 1
fi

DEFINED="$(llvm-nm --defined-only "$PLUGIN_SO" | awk '{print $NF}')"

STATUS=0
for sym in $DEFINED; do
  allowed=0
  for want in $ALLOWLIST; do
    if [ "$sym" = "$want" ]; then
      allowed=1
      break
    fi
  done
  if [ "$allowed" -eq 0 ]; then
    echo "FAIL: unexpected exported symbol: $sym" >&2
    STATUS=1
  fi
done

# The single entry point must be present.
echo "$DEFINED" | grep -q "MusePluginEntry" || {
  echo "FAIL: MusePluginEntry not in defined symbols" >&2
  STATUS=1
}

if [ "$STATUS" -eq 0 ]; then
  echo "PASS: only declared symbols are visible"
fi
exit "$STATUS"
