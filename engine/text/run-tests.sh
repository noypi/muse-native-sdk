#!/bin/sh
# run-tests.sh — configure engine/text into a scratch dir under /tmp, build,
# run one CTest target plus the binary directly for per-case lines.
# Usage: sh engine/text/run-tests.sh <ctest-name>
set -eu
set -o pipefail 2>/dev/null || true

usage() {
  echo "usage: $0 jni_free_path_test|system_fonts_test|layout_cache_test|harfbuzz_parity_test|script_coverage_test|emoji_test|font_fallback_test|uax_conformance_test|pipeline_acceptance" >&2
}

if [ "$#" -ne 1 ]; then
  usage
  exit 2
fi

case "$1" in
  jni_free_path_test|system_fonts_test|layout_cache_test|harfbuzz_parity_test|script_coverage_test|emoji_test|font_fallback_test|uax_conformance_test|pipeline_acceptance)
    ;;
  *)
    usage
    exit 2
    ;;
esac

MODDIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SCRATCH=$(mktemp -d /tmp/text-test-XXXXXX)
echo "scratch: $SCRATCH"

cmake -S "$MODDIR" -B "$SCRATCH"
cmake --build "$SCRATCH"
(cd "$SCRATCH" && ctest -R "^$1$" --output-on-failure)
"$SCRATCH/$1"
