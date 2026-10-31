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
BIN_MEDIA_IMAGE_CACHE=$(mktemp /tmp/media-image_cache_test.XXXXXX)
BIN_MEDIA_COLOR_SPACE_UPLOAD=$(mktemp /tmp/media-color_space_upload_test.XXXXXX)
BIN_MEDIA_ANIMATED_DECODE=$(mktemp /tmp/media-animated_decode_test.XXXXXX)
BIN_MEDIA_ANIMATED_HDR=$(mktemp /tmp/media-animated_hdr_test.XXXXXX)
BIN_FETCH_BRIDGE=$(mktemp /tmp/media-fetch_bridge_test.XXXXXX)
trap 'rm -f "$BIN_MEDIA_WORKER_POOL" "$BIN_MEDIA_DECODE_METADATA" "$BIN_MEDIA_IMAGE_CACHE" "$BIN_MEDIA_COLOR_SPACE_UPLOAD" "$BIN_MEDIA_ANIMATED_DECODE" "$BIN_MEDIA_ANIMATED_HDR" "$BIN_FETCH_BRIDGE"' EXIT

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . -I engine/core/thread/include -I engine/core/budget/include engine/media/tests/worker_pool_test.cpp engine/media/decode/decode_dispatch.cc engine/core/thread/src/worker_pool.cpp engine/core/budget/src/manager.cpp engine/graphics/textures/texture_pool.cc -o "$BIN_MEDIA_WORKER_POOL"
"$BIN_MEDIA_WORKER_POOL"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . -I engine/core/thread/include -I engine/core/budget/include engine/media/tests/decode_metadata_test.cpp engine/media/decode/image_metadata.cc -o "$BIN_MEDIA_DECODE_METADATA"
"$BIN_MEDIA_DECODE_METADATA"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/image_cache_test.cpp engine/media/src/image_cache.cc -o "$BIN_MEDIA_IMAGE_CACHE"
"$BIN_MEDIA_IMAGE_CACHE"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/color_space_upload_test.cpp engine/media/src/color_upload.cc engine/graphics/color/color_space.cc -o "$BIN_MEDIA_COLOR_SPACE_UPLOAD"
"$BIN_MEDIA_COLOR_SPACE_UPLOAD"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/animated_decode_test.cpp engine/media/src/animated_decode.cc -o "$BIN_MEDIA_ANIMATED_DECODE"
"$BIN_MEDIA_ANIMATED_DECODE"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/animated_hdr_test.cpp engine/media/src/animated_hdr.cc -o "$BIN_MEDIA_ANIMATED_HDR"
"$BIN_MEDIA_ANIMATED_HDR"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . -I engine/core/budget/include -I engine/core/thread/include engine/media/tests/fetch_bridge_test.cpp engine/media/src/fetch_bridge.cc engine/core/budget/src/manager.cpp -o "$BIN_FETCH_BRIDGE"
"$BIN_FETCH_BRIDGE"
