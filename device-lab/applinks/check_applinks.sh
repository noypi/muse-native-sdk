#!/usr/bin/env bash
# check_applinks.sh — on-device App Links verification probe (FR-NAV-006 T5).
#
# Usage: check_applinks.sh <package> <host>
#
# Runs `adb shell pm get-app-links <package>` (and, API-gated and
# failure-tolerant, `adb shell pm verify-app-links --re-verify <package>`),
# then parses the per-host verification state.
#
# Contract (T5 Interface):
#   check_applinks.sh <package> <host> -> exit 0 VERIFIED | exit 1 NOT_VERIFIED | exit 2 DEVICE_ERROR
#   stdout: package=<p> host=<h> status=<verified|not_verified|error> detail=<pm output line>
#
# No live-device pass is claimed by this script alone; TC-NAV-016 stays
# external/device-lab (see README.md).
set -u

usage() {
  cat <<USAGE
usage: check_applinks.sh <package> <host>
  Verify Android App Links state for <host> under <package> via adb.

  exit 0  VERIFIED      host is verified for the package
  exit 1  NOT_VERIFIED  host is not verified (browser fallback)
  exit 2  DEVICE_ERROR  usage error, adb missing, or device offline/error

  stdout (always a single line):
    package=<p> host=<h> status=<verified|not_verified|error> detail=<pm output line>
USAGE
}

if [[ "${1:-}" == "--help" ]] || [[ "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ "$#" -ne 2 ]]; then
  usage >&2
  exit 2
fi

package="$1"
host="$2"

if ! command -v adb >/dev/null 2>&1; then
  echo "package=${package} host=${host} status=error detail=adb not found on PATH"
  echo "error: adb not found on PATH" >&2
  exit 2
fi

# API-gated re-verification trigger (failure-tolerant by design: never gates
# the verdict below). `verify-app-links --re-verify` exists from API 23; when
# the device API level cannot be read, attempt it anyway and ignore failure.
api_level="$(adb shell getprop ro.build.version.sdk 2>/dev/null | tr -cd '0-9')"
if [[ -z "${api_level}" ]] || [[ "${api_level}" -ge 23 ]]; then
  adb shell pm verify-app-links --re-verify "${package}" >/dev/null 2>&1 || true
fi

pm_output="$(adb shell pm get-app-links "${package}" 2>&1)"
pm_code="$?"
if [[ "${pm_code}" -ne 0 ]]; then
  detail="$(printf '%s\n' "${pm_output}" | head -n 1)"
  detail="$(printf '%s' "${detail}" | tr -d '\r')"
  if [[ -z "${detail}" ]]; then
    detail="pm get-app-links failed (exit ${pm_code})"
  fi
  echo "package=${package} host=${host} status=error detail=${detail}"
  exit 2
fi

# Device-error markers can arrive on stdout with exit 0 on some builds.
lower_all="$(printf '%s' "${pm_output}" | tr '[:upper:]' '[:lower:]')"
if [[ "${lower_all}" == *"device offline"* ]] || [[ "${lower_all}" == *"no devices"* ]] || [[ "${lower_all}" == *"unauthorized"* ]] || [[ "${lower_all}" == *"device not found"* ]]; then
  detail="$(printf '%s\n' "${pm_output}" | head -n 1)"
  detail="$(printf '%s' "${detail}" | tr -d '\r')"
  echo "package=${package} host=${host} status=error detail=${detail}"
  exit 2
fi

# First pm output line mentioning the queried host; fall back to the first
# line so `detail=` always carries the pm evidence.
detail="$(printf '%s\n' "${pm_output}" | grep -F "${host}" | head -n 1 || true)"
if [[ -z "${detail}" ]]; then
  first="$(printf '%s\n' "${pm_output}" | head -n 1)"
  if [[ -z "${first}" ]]; then
    first="host ${host} not listed in pm output"
  fi
  detail="${first}"
fi
detail="$(printf '%s' "${detail}" | tr -d '\r')"

lower_detail="$(printf '%s' "${detail}" | tr '[:upper:]' '[:lower:]')"
# NOTE: check "unverified"/"not verified" before "verified" because the
# former contains the latter as a substring.
if [[ "${lower_detail}" == *"unverified"* ]] || [[ "${lower_detail}" == *"not verified"* ]] || [[ "${lower_detail}" == *"not_verified"* ]]; then
  echo "package=${package} host=${host} status=not_verified detail=${detail}"
  exit 1
elif [[ "${lower_detail}" == *"verified"* ]]; then
  echo "package=${package} host=${host} status=verified detail=${detail}"
  exit 0
else
  # Conservative default: anything that is not explicitly verified counts as
  # not verified (browser fallback), never as verified.
  echo "package=${package} host=${host} status=not_verified detail=${detail}"
  exit 1
fi
