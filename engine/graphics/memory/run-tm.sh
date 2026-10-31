#!/bin/sh
# run-tm.sh — hermetic VRAM trim-tier gate (NFR-GRAPHICS-004, AC-GRAPHICS-008 scripted-trim half / TC-GRAPHICS-008).
# Compiles + runs the vram_trim tests with hermetic g++ (no Gradle wrapper in
# repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/memory/run-tm.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TM_TRIM=$(mktemp /tmp/tm-vram_trim.XXXXXX)
trap 'rm -f "$BIN_TM_TRIM"' EXIT

g++ -std=c++17 -I . engine/graphics/memory/vram_trim.test.cpp engine/graphics/memory/vram_trim.cc -o "$BIN_TM_TRIM"
"$BIN_TM_TRIM"
