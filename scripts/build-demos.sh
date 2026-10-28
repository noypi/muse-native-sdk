#!/bin/sh
# build-demos.sh — build/list the sample demos via the muse-native CLI.
#
# Usage: build-demos.sh [--list | <target>]
#   --list    print each demo build target, one per line.
#   <target>  demo target to build (e.g. examples/m3-gallery,
#             examples/flappy).
#
# Delegates to `muse-native build <target>` resolved via PATH and propagates
# the CLI exit code. Each target resolves only its own per-project
# `muse-native.yaml` (`<target>/muse-native.yaml`, no workspace-root
# fallback). No direct toolchain invocation lives here.
set -e -u
set -o pipefail 2>/dev/null || true

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
  echo "Usage: build-demos.sh [--list | <target>]"
  exit 0
fi

if [ "${1:-}" = "--list" ]; then
  echo "examples/m3-gallery"
  echo "examples/flappy"
  exit 0
fi

if [ "$#" -ne 1 ] || [ -z "${1:-}" ]; then
  echo "Usage: build-demos.sh [--list | <target>]" >&2
  exit 2
fi

TARGET="$1"

exec muse-native build "$TARGET"
