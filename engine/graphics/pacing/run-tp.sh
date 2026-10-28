#!/bin/sh
# run-tp.sh — hermetic pacing-batch verification (FR-GRAPHICS-007, TC-GRAPHICS-017/018).
# Compiles + runs the frame_rate and render_ahead tests with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/pacing/run-tp.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TP_RATE=$(mktemp /tmp/tp-frame_rate.XXXXXX)
BIN_TP_AHEAD=$(mktemp /tmp/tp-render_ahead.XXXXXX)
DUAL_A_SRC=$(mktemp /tmp/tp-dual_a.XXXXXX.cpp)
BIN_TP_DUAL_A=$(mktemp /tmp/tp-dual_a.XXXXXX)
DUAL_B_SRC=$(mktemp /tmp/tp-dual_b.XXXXXX.cpp)
BIN_TP_DUAL_B=$(mktemp /tmp/tp-dual_b.XXXXXX)
trap 'rm -f "$BIN_TP_RATE" "$BIN_TP_AHEAD" "$DUAL_A_SRC" "$BIN_TP_DUAL_A" "$DUAL_B_SRC" "$BIN_TP_DUAL_B"' EXIT

g++ -std=c++17 -I . engine/graphics/pacing/frame_rate.test.cpp engine/graphics/pacing/frame_rate.cc -o "$BIN_TP_RATE"
"$BIN_TP_RATE"

g++ -std=c++17 -I . engine/graphics/pacing/render_ahead.test.cpp engine/graphics/pacing/render_ahead.cc -o "$BIN_TP_AHEAD"
"$BIN_TP_AHEAD"

# T7 dual-include TU probes (BLOCKER 2): a single TU including both pacing
# headers must compile in BOTH include orders. Pre-fix this fails with
# `redefinition of 'enum class pacing::Status'`; post-fix (single-owner
# pacing_status.h) both TUs compile cleanly.
cat > "$DUAL_A_SRC" <<'EOF'
#include "engine/graphics/pacing/frame_rate.h"
#include "engine/graphics/pacing/render_ahead.h"
int main() { return 0; }
EOF
g++ -std=c++17 -I . "$DUAL_A_SRC" -o "$BIN_TP_DUAL_A"
"$BIN_TP_DUAL_A"

cat > "$DUAL_B_SRC" <<'EOF'
#include "engine/graphics/pacing/render_ahead.h"
#include "engine/graphics/pacing/frame_rate.h"
int main() { return 0; }
EOF
g++ -std=c++17 -I . "$DUAL_B_SRC" -o "$BIN_TP_DUAL_B"
"$BIN_TP_DUAL_B"
