#!/bin/sh
# run-media.sh — hermetic media-batch verification (NFR-MEDIA-001, FR-MEDIA-001).
# Compiles + runs the worker_pool and decode_metadata tests with hermetic g++
# (no Gradle wrapper in repo). Exits nonzero on any compile or test failure.
# Usage: sh engine/media/run-media.sh   (from the checkout root)
# CI alias: ./gradlew :media:workerPoolTest
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

BIN_MEDIA_WORKER_POOL=$(mktemp /tmp/media-worker_pool_test.XXXXXX)
BIN_MEDIA_DECODE_METADATA=$(mktemp /tmp/media-decode_metadata_test.XXXXXX)
trap 'rm -f "$BIN_MEDIA_WORKER_POOL" "$BIN_MEDIA_DECODE_METADATA"' EXIT

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . -I engine/core/thread/include -I engine/core/budget/include engine/media/tests/worker_pool_test.cpp engine/media/decode/decode_dispatch.cc engine/core/thread/src/worker_pool.cpp engine/core/budget/src/manager.cpp engine/graphics/textures/texture_pool.cc -o "$BIN_MEDIA_WORKER_POOL"
"$BIN_MEDIA_WORKER_POOL"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . -I engine/core/thread/include -I engine/core/budget/include engine/media/tests/decode_metadata_test.cpp engine/media/decode/image_metadata.cc -o "$BIN_MEDIA_DECODE_METADATA"
"$BIN_MEDIA_DECODE_METADATA"
