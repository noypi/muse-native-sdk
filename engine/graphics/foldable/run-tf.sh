#!/bin/sh
# run-tf.sh — hermetic foldable API-usage gate (NFR-GRAPHICS-002, AC-GRAPHICS-004 / TC-GRAPHICS-004).
# Compiles + runs the foldable_api bridge unit test with hermetic g++ and
# enforces the static source-usage gate: zero ADisplayFeature matches under
# engine/, and the graphics foldable bridge references
# WindowManager.getCurrentWindowMetrics / WindowMetricsCalculator /
# WindowLayoutInfo / FoldingFeature / TYPE_HINGE_ANGLE.
# Exits 0 iff the static gate and the unit test both pass; nonzero + reason
# line otherwise. Runnable on any host, no device needed.
# Usage: sh engine/graphics/foldable/run-tf.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

if grep -rn "ADisplayFeature" engine/ 2>/dev/null | grep -v "engine/graphics/foldable/run-tf.sh" | grep -v "engine/graphics/foldable/foldable_api.test.cpp"; then
  echo "FAIL run-tf: obsolete ADisplayFeature naming found under engine/ (AC-GRAPHICS-004 ban)"
  exit 1
fi
echo "PASS run-tf: zero ADisplayFeature matches under engine/"

for tok in "WindowManager.getCurrentWindowMetrics" "WindowMetricsCalculator" "WindowLayoutInfo" "FoldingFeature" "TYPE_HINGE_ANGLE"; do
  if ! grep -rh "$tok" engine/graphics/foldable/foldable_api.h engine/graphics/foldable/foldable_api.cc 2>/dev/null | grep -q .; then
    echo "FAIL run-tf: required API token '$tok' not referenced under engine/graphics/foldable/"
    exit 1
  fi
  echo "PASS run-tf: token '$tok' referenced"
done

BIN_TF_FOLDABLE=$(mktemp /tmp/tf-foldable_api.XXXXXX)
trap 'rm -f "$BIN_TF_FOLDABLE"' EXIT

g++ -std=c++17 -I . engine/graphics/foldable/foldable_api.test.cpp engine/graphics/foldable/foldable_api.cc -o "$BIN_TF_FOLDABLE"
"$BIN_TF_FOLDABLE"
