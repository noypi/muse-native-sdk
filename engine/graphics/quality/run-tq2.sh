#!/bin/sh
# run-tq2.sh — hermetic quality-telemetry verification (NFR-GRAPHICS-005, TC-GRAPHICS-027).
# Compiles + runs the quality_telemetry test with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Naming follows the run-tq.sh family (plan T8 names run-tq.sh; this script
# extends that family for the T7 telemetry item).
# Usage: sh engine/graphics/quality/run-tq2.sh   (from the checkout root)
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT"

BIN_TQ_TELEMETRY=$(mktemp /tmp/tq-quality_telemetry.XXXXXX)
trap 'rm -f "$BIN_TQ_TELEMETRY"' EXIT

g++ -std=c++17 -I . engine/graphics/quality/quality_telemetry.test.cpp engine/graphics/quality/quality_telemetry.cc engine/graphics/quality/quality_tier.cc -o "$BIN_TQ_TELEMETRY"
"$BIN_TQ_TELEMETRY"
