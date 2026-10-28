#!/usr/bin/env bash
# sanitizer-ci.sh — ASan CI gate for the SPEC-CORE-ENGINE memory-safety
# tooling (NFR-CORE-005 / NFR-CORE-011; AC-CORE-027, AC-CORE-030).
#
# Usage: sanitizer-ci.sh [--build-dir <dir>]
#   Exit 0 iff ALL of the following hold:
#     (a) the safety suites build under -DMUSE_SANITIZER=asan,
#     (b) the seeded UAF/OOB cases produce the expected ASan abort
#         signatures (heap-use-after-free, heap-buffer-overflow),
#     (c) safety_zeroize passes clean (no sanitizer findings),
#     (d) the existing engine/core/bridge fuzz suite (bridge_fuzz, 8/8)
#         runs clean under ASan.
#   Any failure prints the failing stage ("STAGE FAILED: <stage>") and
#   exits nonzero. All stage output is captured to <dir>/sanitizer-ci.log.
#
# CI scope note: this gate is ASan-only. HWASan and MTE are excluded from
# CI scope by design — HWASan needs a Clang/AArch64 toolchain and MTE
# needs Armv8.5-A+memtag hardware; MTE verification is covered by the T4
# manual on-device procedure (engine/core/safety/MTE_QA_PROCEDURE.md).
# This script is hermetic on a Linux NDK-less host build: no downloads
# (no curl/wget/git clone/package installs), no device, no secrets.
#
# The bridge configure injects ASan flags via CMAKE_CXX_FLAGS /
# CMAKE_EXE_LINKER_FLAGS using the same flag spelling as the T1 matrix
# (cmake/Sanitizers.cmake: -fsanitize=address -fno-omit-frame-pointer
# plus the -fstack-protector-strong / -D_FORTIFY_SOURCE=3 baseline),
# because engine/core/bridge does not include(Sanitizers) itself.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../../.." && pwd)"

BUILD_DIR=""
while [ $# -gt 0 ]; do
  case "$1" in
    --build-dir)
      [ $# -ge 2 ] || { echo "usage: sanitizer-ci.sh [--build-dir <dir>]" >&2; exit 2; }
      BUILD_DIR="$2"
      shift 2
      ;;
    -h|--help)
      echo "usage: sanitizer-ci.sh [--build-dir <dir>]"
      exit 0
      ;;
    *)
      echo "usage: sanitizer-ci.sh [--build-dir <dir>]" >&2
      echo "STAGE FAILED: usage" >&2
      exit 2
      ;;
  esac
done

if [ -z "${BUILD_DIR}" ]; then
  BUILD_DIR="$(mktemp -d -t sanitizer-ci.XXXXXX)"
  CREATED_TMP=1
else
  CREATED_TMP=0
fi
if ! mkdir -p "${BUILD_DIR}" 2>/dev/null || [ ! -w "${BUILD_DIR}" ]; then
  echo "STAGE FAILED: usage" >&2
  echo "sanitizer-ci: --build-dir '${BUILD_DIR}' missing or unwritable" >&2
  exit 2
fi

LOG="${BUILD_DIR}/sanitizer-ci.log"
: > "${LOG}"

SAFETY_BUILD="${BUILD_DIR}/safety-asan"
BRIDGE_BUILD="${BUILD_DIR}/bridge-asan"

# Same ASan spelling as cmake/Sanitizers.cmake (T1 matrix) for the bridge
# configure, which cannot include(Sanitizers) itself.
ASAN_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -fstack-protector-strong -D_FORTIFY_SOURCE=3"
ASAN_LD_FLAGS="-fsanitize=address -fstack-protector-strong"

log() {
  echo "$@" | tee -a "${LOG}"
}

fail() {
  echo "STAGE FAILED: $1" | tee -a "${LOG}" >&2
  exit 1
}

# --- Stage 1: safety-asan-build ---
log "=== STAGE safety-asan-build ==="
{
  echo "\$ cmake -S engine/core/safety -B <dir>/safety-asan -DMUSE_SANITIZER=asan"
  echo "\$ cmake --build <dir>/safety-asan"
} >> "${LOG}"
if ! cmake -S "${REPO_ROOT}/engine/core/safety" -B "${SAFETY_BUILD}" \
    -DMUSE_SANITIZER=asan >>"${LOG}" 2>&1; then
  fail safety-asan-build
fi
if ! cmake --build "${SAFETY_BUILD}" >>"${LOG}" 2>&1; then
  fail safety-asan-build
fi
log "STAGE PASSED: safety-asan-build"

# --- Stage 2: safety-uaf-seed (negative test: must abort under ASan) ---
log "=== STAGE safety-uaf-seed ==="
UAF_OUT="${BUILD_DIR}/uaf-seed.out"
set +e
ASAN_OPTIONS=detect_leaks=0 "${SAFETY_BUILD}/safety_uaf" --seed >"${UAF_OUT}" 2>&1
UAF_RC=$?
set -e
cat "${UAF_OUT}" >>"${LOG}"
if ! grep -q "heap-use-after-free" "${UAF_OUT}"; then
  log "(safety_uaf --seed exit=${UAF_RC}; expected ASan heap-use-after-free abort)"
  fail safety-uaf-seed
fi
log "STAGE PASSED: safety-uaf-seed (heap-use-after-free signature found)"
log "heap-use-after-free"

# --- Stage 3: safety-oob-seed (negative test: must abort under ASan) ---
log "=== STAGE safety-oob-seed ==="
OOB_OUT="${BUILD_DIR}/oob-seed.out"
set +e
ASAN_OPTIONS=detect_leaks=0 "${SAFETY_BUILD}/safety_oob" --seed >"${OOB_OUT}" 2>&1
OOB_RC=$?
set -e
cat "${OOB_OUT}" >>"${LOG}"
if ! grep -q "heap-buffer-overflow" "${OOB_OUT}"; then
  log "(safety_oob --seed exit=${OOB_RC}; expected ASan heap-buffer-overflow abort)"
  fail safety-oob-seed
fi
log "STAGE PASSED: safety-oob-seed (heap-buffer-overflow signature found)"
log "heap-buffer-overflow"

# --- Stage 4: safety-zeroize-clean ---
log "=== STAGE safety-zeroize-clean ==="
if ! ctest --test-dir "${SAFETY_BUILD}" -R safety_zeroize --output-on-failure >>"${LOG}" 2>&1; then
  fail safety-zeroize-clean
fi
log "STAGE PASSED: safety-zeroize-clean (safety_zeroize pass)"

# --- Stage 5: bridge-fuzz-asan ---
log "=== STAGE bridge-fuzz-asan ==="
{
  echo "\$ cmake -S engine/core/bridge -B <dir>/bridge-asan -DCMAKE_CXX_FLAGS=\"${ASAN_CXX_FLAGS}\" -DCMAKE_EXE_LINKER_FLAGS=\"${ASAN_LD_FLAGS}\""
  echo "\$ cmake --build <dir>/bridge-asan"
  echo "\$ ctest --test-dir <dir>/bridge-asan -R bridge_fuzz"
} >> "${LOG}"
if ! cmake -S "${REPO_ROOT}/engine/core/bridge" -B "${BRIDGE_BUILD}" \
    -DCMAKE_CXX_FLAGS="${ASAN_CXX_FLAGS}" \
    -DCMAKE_EXE_LINKER_FLAGS="${ASAN_LD_FLAGS}" >>"${LOG}" 2>&1; then
  fail bridge-fuzz-asan
fi
if ! cmake --build "${BRIDGE_BUILD}" >>"${LOG}" 2>&1; then
  fail bridge-fuzz-asan
fi
FUZZ_OUT="${BUILD_DIR}/bridge-fuzz.out"
set +e
ctest --test-dir "${BRIDGE_BUILD}" -R bridge_fuzz --output-on-failure >"${FUZZ_OUT}" 2>&1
CTEST_RC=$?
ASAN_OPTIONS=detect_leaks=0 "${BRIDGE_BUILD}/bridge_fuzz_test" >>"${FUZZ_OUT}" 2>&1
FUZZ_RC=$?
set -e
cat "${FUZZ_OUT}" >>"${LOG}"
PASS_COUNT="$(grep -c "^PASS " "${FUZZ_OUT}" || true)"
if [ "${CTEST_RC}" -ne 0 ] || [ "${FUZZ_RC}" -ne 0 ] || [ "${PASS_COUNT}" != "8" ]; then
  log "(bridge_fuzz: ctest_rc=${CTEST_RC} fuzz_rc=${FUZZ_RC} pass_lines=${PASS_COUNT}; expected 8/8 pass)"
  fail bridge-fuzz-asan
fi
log "STAGE PASSED: bridge-fuzz-asan (bridge_fuzz 8/8 pass)"
log "bridge_fuzz 8/8 pass"

log "ALL STAGES PASSED"
exit 0
