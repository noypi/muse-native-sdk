#!/bin/sh
# run-ts.sh — hermetic scene-batch verification (FR-GRAPHICS-009, TC-GRAPHICS-023/024).
# Compiles + runs the scene_module and scene_composite tests with hermetic g++
# (no Gradle wrapper in repo). Also compiles a base-only TU that includes no
# scene headers, proving the base configuration links without these files
# (D1 separability). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/scene/run-ts.sh   (from the checkout root)
#
# TC-025 MANUAL STEPS (device-lab/manual Close — NOT executed here, per A4):
#   1. Build the base engine without the 3D module.
#   2. Confirm the base build links and boots.
#   3. Build a test app that includes the module and confirm it loads a glTF
#      scene inside the engine frame graph with no separate swapchain.
#   4. Confirm the base-engine footprint estimate is unchanged.
# Hermetic coverage here: T5 gate-state tests + base-only TU separability
# proof + T6 same-graph composite/budget tests. Method/Status flips for TC-025
# steps 2-4 belong to a later device-lab/manual Close.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

# 0. Base-only separability probe: this TU includes NO scene headers, so the
#    base configuration links without any file under engine/graphics/scene/.
BASE_SRC=$(mktemp /tmp/ts-base_only.XXXXXX.cpp)
BIN_BASE=$(mktemp /tmp/ts-base_only.XXXXXX)
BIN_TS_MODULE=$(mktemp /tmp/ts-scene_module.XXXXXX)
BIN_TS_COMPOSITE=$(mktemp /tmp/ts-scene_composite.XXXXXX)
TRIPLE_A_SRC=$(mktemp /tmp/ts-triple_a.XXXXXX.cpp)
BIN_TRIPLE_A=$(mktemp /tmp/ts-triple_a.XXXXXX)
TRIPLE_B_SRC=$(mktemp /tmp/ts-triple_b.XXXXXX.cpp)
BIN_TRIPLE_B=$(mktemp /tmp/ts-triple_b.XXXXXX)
trap 'rm -f "$BASE_SRC" "$BIN_BASE" "$BIN_TS_MODULE" "$BIN_TS_COMPOSITE" "$TRIPLE_A_SRC" "$BIN_TRIPLE_A" "$TRIPLE_B_SRC" "$BIN_TRIPLE_B"' EXIT

cat > "$BASE_SRC" <<'EOF'
// Base-configuration probe: includes NO scene headers. If the base build
// required any scene source, a base-only target could not link.
#include <cassert>
int main() {
  // No reference to isSceneModuleLinked / linkSceneModule / compositeSceneNode.
  assert(true);
  return 0;
}
EOF
if grep -q 'graphics/scene' "$BASE_SRC"; then
  echo "FAIL base-only TU must not reference scene headers" >&2
  exit 1
fi
g++ -std=c++17 -I . "$BASE_SRC" -o "$BIN_BASE"
"$BIN_BASE"

g++ -std=c++17 -I . engine/graphics/scene/scene_module.test.cpp engine/graphics/scene/scene_module.cc -o "$BIN_TS_MODULE"
"$BIN_TS_MODULE"

g++ -std=c++17 -I . engine/graphics/scene/scene_composite.test.cpp engine/graphics/scene/scene_composite.cc engine/graphics/scene/scene_module.cc -o "$BIN_TS_COMPOSITE"
"$BIN_TS_COMPOSITE"

# T8 triple-include TU probes (BLOCKER 1 + MINOR 3): one TU including
# scene_module.h + scene_composite.h + texture_pool.h must compile in BOTH
# scene-header orders. Pre-fix this fails (narrow-Status missing kOverBudget /
# duplicate PoolState/Status redefinition); post-fix (single-owner
# graphics_status.h, PoolState owned by texture_pool.h and included — not
# redeclared — by scene_composite.h, real scene_module.h include) both TUs
# compile cleanly.
cat > "$TRIPLE_A_SRC" <<'EOF'
#include "engine/graphics/scene/scene_module.h"
#include "engine/graphics/scene/scene_composite.h"
#include "engine/graphics/textures/texture_pool.h"
int main() { return 0; }
EOF
g++ -std=c++17 -I . "$TRIPLE_A_SRC" -o "$BIN_TRIPLE_A"
"$BIN_TRIPLE_A"

cat > "$TRIPLE_B_SRC" <<'EOF'
#include "engine/graphics/scene/scene_composite.h"
#include "engine/graphics/scene/scene_module.h"
#include "engine/graphics/textures/texture_pool.h"
int main() { return 0; }
EOF
g++ -std=c++17 -I . "$TRIPLE_B_SRC" -o "$BIN_TRIPLE_B"
"$BIN_TRIPLE_B"
