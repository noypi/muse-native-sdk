#!/bin/sh
# run-tv.sh — hermetic vector-batch verification (FR-GRAPHICS-001, TC-GRAPHICS-002).
# Compiles + runs the tile_raster and slug_msdf tests with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/vector/run-tv.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

g++ -std=c++17 -I . engine/graphics/vector/tile_raster.test.cpp engine/graphics/vector/tile_raster.cc -o /tmp/tv-tile_raster.test
/tmp/tv-tile_raster.test

g++ -std=c++17 -I . engine/graphics/vector/slug_msdf.test.cpp engine/graphics/vector/slug_msdf.cc -o /tmp/tv-slug_msdf.test
/tmp/tv-slug_msdf.test
