#!/bin/sh
# run-tv.sh — hermetic vector-batch verification (FR-GRAPHICS-001, TC-GRAPHICS-002).
# Compiles + runs the tile_raster and slug_msdf tests with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/vector/run-tv.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TV_RASTER=$(mktemp /tmp/tv-tile_raster.XXXXXX)
BIN_TV_SLUG=$(mktemp /tmp/tv-slug_msdf.XXXXXX)
trap 'rm -f "$BIN_TV_RASTER" "$BIN_TV_SLUG"' EXIT

g++ -std=c++17 -I . engine/graphics/vector/tile_raster.test.cpp engine/graphics/vector/tile_raster.cc -o "$BIN_TV_RASTER"
"$BIN_TV_RASTER"

g++ -std=c++17 -I . engine/graphics/vector/slug_msdf.test.cpp engine/graphics/vector/slug_msdf.cc -o "$BIN_TV_SLUG"
"$BIN_TV_SLUG"
