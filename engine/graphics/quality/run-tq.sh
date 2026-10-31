#!/bin/sh
# run-tq.sh — hermetic quality-tier gate (NFR-GRAPHICS-005, AC-GRAPHICS-026/027 / TC-GRAPHICS-026/027).
# Compiles + runs the quality_tier tests with hermetic g++ (no Gradle wrapper
# in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/graphics/quality/run-tq.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TQ_TIER=$(mktemp /tmp/tq-quality_tier.XXXXXX)
trap 'rm -f "$BIN_TQ_TIER"' EXIT

g++ -std=c++17 -I . engine/graphics/quality/quality_tier.test.cpp engine/graphics/quality/quality_tier.cc -o "$BIN_TQ_TIER"
"$BIN_TQ_TIER"
