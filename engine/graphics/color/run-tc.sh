#!/bin/sh
# run-tc.sh — hermetic color-batch verification (FR-GRAPHICS-002/004, TC-GRAPHICS-003).
# Compiles + runs the color_space and filter_graph tests with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/color/run-tc.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

g++ -std=c++17 -I . engine/graphics/color/color_space.test.cpp engine/graphics/color/color_space.cc -o /tmp/tc-color_space.test
/tmp/tc-color_space.test

g++ -std=c++17 -I . engine/graphics/color/filter_graph.test.cpp engine/graphics/color/filter_graph.cc -o /tmp/tc-filter_graph.test
/tmp/tc-filter_graph.test
