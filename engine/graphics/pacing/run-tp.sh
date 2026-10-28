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
trap 'rm -f "$BIN_TP_RATE" "$BIN_TP_AHEAD"' EXIT

g++ -std=c++17 -I . engine/graphics/pacing/frame_rate.test.cpp engine/graphics/pacing/frame_rate.cc -o "$BIN_TP_RATE"
"$BIN_TP_RATE"

g++ -std=c++17 -I . engine/graphics/pacing/render_ahead.test.cpp engine/graphics/pacing/render_ahead.cc -o "$BIN_TP_AHEAD"
"$BIN_TP_AHEAD"
