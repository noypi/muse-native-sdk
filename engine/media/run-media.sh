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
BIN_CAPS=$(mktemp /tmp/media-host_copy_caps_acceptance_test.XXXXXX)
BIN_MEDIA_LIFECYCLE_AUDIO_FOCUS=$(mktemp /tmp/media-lifecycle_audio_focus_test.XXXXXX)
BIN_MEDIA_AUDIO_ENGINE=$(mktemp /tmp/media-audio_engine_test.XXXXXX)
BIN_MEDIA_ZERO_COPY_FRAME=$(mktemp /tmp/media-zero_copy_frame_test.XXXXXX)
BIN_MEDIA_DRM_SESSION=$(mktemp /tmp/media-drm_session_test.XXXXXX)
BIN_MEDIA_DOWNLOAD_RESUME=$(mktemp /tmp/media-download_resume_test.XXXXXX)
BIN_MEDIA_PLAYBACK_PIPELINE=$(mktemp /tmp/media-playback_pipeline_test.XXXXXX)
BIN_PHASED_MEDIA3=$(mktemp /tmp/media-phased_media3_test.XXXXXX)
BIN_PHASED_CODECS=$(mktemp /tmp/media-phased_codecs_test.XXXXXX)
trap 'rm -f "$BIN_MEDIA_WORKER_POOL" "$BIN_MEDIA_DECODE_METADATA" "$BIN_MEDIA_IMAGE_CACHE" "$BIN_MEDIA_COLOR_SPACE_UPLOAD" "$BIN_MEDIA_ANIMATED_DECODE" "$BIN_MEDIA_ANIMATED_HDR" "$BIN_FETCH_BRIDGE" "$BIN_CAPS" "$BIN_MEDIA_LIFECYCLE_AUDIO_FOCUS" "$BIN_MEDIA_AUDIO_ENGINE" "$BIN_MEDIA_ZERO_COPY_FRAME" "$BIN_MEDIA_DRM_SESSION" "$BIN_MEDIA_DOWNLOAD_RESUME" "$BIN_MEDIA_PLAYBACK_PIPELINE" "$BIN_PHASED_MEDIA3" "$BIN_PHASED_CODECS"' EXIT

# T9 (FR-MEDIA-013/AC-MEDIA-020): Media3-first phase gate.
# `sh engine/media/run-media.sh phased_media3` exits 0 iff the FR-MEDIA-010
# acceptance set is green (AC-015 drm_session, AC-016 download_resume,
# AC-017 playback_pipeline) AND FFmpeg/WebRTC symbols are absent from the
# base artifact. Later phases (FFmpeg LGPL-dynamic, WebRTC dynamic feature)
# must not leak into base until their admission gates land (T10).
if [ "${1:-all}" = "phased_media3" ]; then
  g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/phased_media3_test.cpp -o "$BIN_PHASED_MEDIA3"
  "$BIN_PHASED_MEDIA3"

  # FR-MEDIA-010 acceptance set green: re-establish the evidence here
  # (fail closed — any red denies the phase-1 gate).
  g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/drm_session_test.cpp engine/media/src/drm_session.cc -o "$BIN_MEDIA_DRM_SESSION"
  "$BIN_MEDIA_DRM_SESSION"
  g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/download_resume_test.cpp engine/media/src/download_resume.cc -o "$BIN_MEDIA_DOWNLOAD_RESUME"
  "$BIN_MEDIA_DOWNLOAD_RESUME"
  g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/playback_pipeline_test.cpp engine/media/src/playback_pipeline.cc -o "$BIN_MEDIA_PLAYBACK_PIPELINE"
  "$BIN_MEDIA_PLAYBACK_PIPELINE"

  # FFmpeg absent from the base artifact (source + linked-symbol check).
  if grep -rniE 'ffmpeg|avcodec|avformat|avutil|swscale|swresample' engine/media/src engine/media/decode engine/media/audio engine/media/include 2>/dev/null; then
    echo "FAIL phased_media3: ffmpeg symbols present in base artifact" >&2
    exit 1
  fi
  if command -v nm >/dev/null 2>&1; then
    if nm "$BIN_MEDIA_DRM_SESSION" "$BIN_MEDIA_DOWNLOAD_RESUME" "$BIN_MEDIA_PLAYBACK_PIPELINE" 2>/dev/null | grep -iE 'ffmpeg|avcodec|avformat|avutil|swscale|swresample'; then
      echo "FAIL phased_media3: ffmpeg symbols linked into base test binaries" >&2
      exit 1
    fi
  fi

  # WebRTC absent from the base artifact (source + linked-symbol check).
  if grep -rniE 'webrtc|libwebrtc|peerconnection|PeerConnection' engine/media/src engine/media/decode engine/media/audio engine/media/include 2>/dev/null; then
    echo "FAIL phased_media3: webrtc symbols present in base artifact" >&2
    exit 1
  fi
  if command -v nm >/dev/null 2>&1; then
    if nm "$BIN_MEDIA_DRM_SESSION" "$BIN_MEDIA_DOWNLOAD_RESUME" "$BIN_MEDIA_PLAYBACK_PIPELINE" 2>/dev/null | grep -iE 'webrtc|libwebrtc|peerconnection'; then
      echo "FAIL phased_media3: webrtc symbols linked into base test binaries" >&2
      exit 1
    fi
  fi

  echo "ALL GREEN phased_media3 gate (FR-MEDIA-010 green; ffmpeg/webrtc absent from base)"
  exit 0
fi

# T10 (FR-MEDIA-013/AC-MEDIA-021; required FR-DEPS-007 AC-017/018):
# later-phase admission gate. `sh engine/media/run-media.sh phased_codecs`
# exits 0 iff the FFmpeg LGPL-dynamic admission gates are green
# (shared-library link, no --enable-gpl / --enable-nonfree, configure line
# recorded in the SBOM) and the WebRTC dynamic-feature packaging gate is
# green, AND the earlier phase-1 gate is unregressed. Later phases stay held
# out until every clause below passes (fail closed).
if [ "${1:-all}" = "phased_codecs" ]; then
  g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/phased_codecs_test.cpp -o "$BIN_PHASED_CODECS"
  "$BIN_PHASED_CODECS"

  # FR-DEPS-007 AC-017/018: FFmpeg LGPL-dynamic admission — shared-library
  # link with its configure line recorded; refuses --enable-gpl and
  # --enable-nonfree, naming the offending flag (fail closed).
  python3 tools/deps/ffmpeg_check.py --manifest build/dependency-manifest.json
  if grep -qF -- '--enable-gpl' build/dependency-manifest.json 2>/dev/null; then
    echo "FAIL phased_codecs: FFmpeg configure refuses GPL flag --enable-gpl" >&2
    exit 1
  fi
  if grep -qF -- '--enable-nonfree' build/dependency-manifest.json 2>/dev/null; then
    echo "FAIL phased_codecs: FFmpeg configure refuses nonfree flag --enable-nonfree" >&2
    exit 1
  fi

  # AC-DEPS-017: the FFmpeg configure line recorded in the dependency
  # manifest (the SBOM source) must be carried by any generated SBOM, so the
  # LGPL configure stays auditable. No generated SBOM yet: the manifest
  # record itself satisfies the gate; a generated SBOM that drops the line
  # denies admission.
  FF_CONFIGURE=$(python3 -c "import json; m=json.load(open('build/dependency-manifest.json')); print(next((e.get('configure','') for e in m.get('dependencies',[]) if isinstance(e,dict) and 'ffmpeg' in str(e.get('name','')).lower()),''))")
  if [ -z "$FF_CONFIGURE" ]; then
    echo "FAIL phased_codecs: FFmpeg SBOM configure line missing from manifest" >&2
    exit 1
  fi
  for SBOM in dist/sbom.json build/sbom.json sbom.json build/sbom.spdx.json; do
    if [ -f "$SBOM" ]; then
      if ! grep -qF -- "$FF_CONFIGURE" "$SBOM"; then
        echo "FAIL phased_codecs: FFmpeg configure line not carried by SBOM $SBOM" >&2
        exit 1
      fi
    fi
  done

  # AC-MEDIA-021: WebRTC admits only as dynamic-feature packaging — the
  # manifest entry must exist with a dynamic link (static bundling into the
  # base artifact denies admission).
  if ! python3 -c "import json,sys; m=json.load(open('build/dependency-manifest.json')); w=[e for e in m.get('dependencies',[]) if isinstance(e,dict) and str(e.get('name','')).lower()=='webrtc']; sys.exit(0 if (w and str(w[0].get('link','')).lower() in ('dynamic','shared')) else 1)"; then
    echo "FAIL phased_codecs: WebRTC dynamic feature packaging gate red (manifest link must be dynamic)" >&2
    exit 1
  fi

  # AC-MEDIA-021 no-regression: re-establish the phase-1 gate here
  # (fail closed — any red denies later-phase admission).
  sh engine/media/run-media.sh phased_media3

  echo "ALL GREEN phased_codecs gate (FFmpeg LGPL-dynamic + WebRTC dynamic-feature gates green; phased_media3 unregressed)"
  exit 0
fi

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

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/host_copy_caps_acceptance_test.cpp engine/media/src/image_cache.cc -o "$BIN_CAPS"
"$BIN_CAPS"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/lifecycle_audio_focus_test.cpp -o "$BIN_MEDIA_LIFECYCLE_AUDIO_FOCUS"
"$BIN_MEDIA_LIFECYCLE_AUDIO_FOCUS"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/audio_engine_test.cpp -o "$BIN_MEDIA_AUDIO_ENGINE"
"$BIN_MEDIA_AUDIO_ENGINE"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/zero_copy_frame_test.cpp engine/media/src/zero_copy_frame.cc -o "$BIN_MEDIA_ZERO_COPY_FRAME"
"$BIN_MEDIA_ZERO_COPY_FRAME"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/drm_session_test.cpp engine/media/src/drm_session.cc -o "$BIN_MEDIA_DRM_SESSION"
"$BIN_MEDIA_DRM_SESSION"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/download_resume_test.cpp engine/media/src/download_resume.cc -o "$BIN_MEDIA_DOWNLOAD_RESUME"
"$BIN_MEDIA_DOWNLOAD_RESUME"

g++ -std=c++17 -Wall -Wextra -Wpedantic -I . engine/media/tests/playback_pipeline_test.cpp engine/media/src/playback_pipeline.cc -o "$BIN_MEDIA_PLAYBACK_PIPELINE"
"$BIN_MEDIA_PLAYBACK_PIPELINE"
