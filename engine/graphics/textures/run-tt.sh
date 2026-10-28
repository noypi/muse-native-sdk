#!/bin/sh
# run-tt.sh — hermetic textures-batch verification (FR-GRAPHICS-008, TC-GRAPHICS-020/021/022).
# Compiles + runs the texture_format and texture_pool tests with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/textures/run-tt.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TT_FORMAT=$(mktemp /tmp/tt-texture_format.XXXXXX)
BIN_TT_POOL=$(mktemp /tmp/tt-texture_pool.XXXXXX)
trap 'rm -f "$BIN_TT_FORMAT" "$BIN_TT_POOL"' EXIT

g++ -std=c++17 -I . engine/graphics/textures/texture_format.test.cpp engine/graphics/textures/texture_format.cc -o "$BIN_TT_FORMAT"
"$BIN_TT_FORMAT"

g++ -std=c++17 -I . engine/graphics/textures/texture_pool.test.cpp engine/graphics/textures/texture_pool.cc -o "$BIN_TT_POOL"
"$BIN_TT_POOL"
