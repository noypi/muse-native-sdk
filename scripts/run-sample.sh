#!/bin/sh
# run-sample.sh — launch the sample app via the muse-native CLI.
#
# Usage: run-sample.sh [target]
#   target   sample target to launch; defaults to examples/m3-gallery.
#
# Delegates to `muse-native run <target>` resolved via PATH and propagates
# the CLI exit code. No direct toolchain invocation lives here.
set -e -u
set -o pipefail 2>/dev/null || true

TARGET="${1:-examples/m3-gallery}"
exec muse-native run "$TARGET"
