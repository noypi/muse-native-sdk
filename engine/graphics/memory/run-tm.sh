#!/bin/sh
# run-tm.sh — hermetic memory-batch verification (NFR-GRAPHICS-004, AC-GRAPHICS-008 / TC-GRAPHICS-008).
# Compiles + runs the vram_trim tests and the vram_budget test with hermetic
# g++ (T5 extends this script with the vram_soak test; no Gradle wrapper in
# repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/memory/run-tm.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TM_TRIM=$(mktemp /tmp/tm-vram_trim.XXXXXX)
BIN_TM_BUDGET=$(mktemp /tmp/tm-vram_budget.XXXXXX)
trap 'rm -f "$BIN_TM_TRIM" "$BIN_TM_BUDGET"' EXIT

g++ -std=c++17 -I . engine/graphics/memory/vram_trim.test.cpp engine/graphics/memory/vram_trim.cc -o "$BIN_TM_TRIM"
"$BIN_TM_TRIM"

g++ -std=c++17 -I . engine/graphics/memory/vram_budget.test.cpp engine/graphics/memory/vram_budget.cc -o "$BIN_TM_BUDGET"
"$BIN_TM_BUDGET"
