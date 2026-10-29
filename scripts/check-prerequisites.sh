#!/bin/sh
# check-prerequisites.sh — fail-closed setup gate for JDK, Android SDK, NDK, device.
#
# Usage: check-prerequisites.sh [--self-test]
#   (no args)  probe each prerequisite, print one OK/MISSING line per item,
#              exit 0 when all present, exit 1 naming each missing item + fix.
#   --self-test  simulate every missing item (no farm needed), print one
#              MISSING + fix line per prerequisite, exit 1.
#
# Probes honor PATH and JAVA_HOME / ANDROID_HOME / ANDROID_SDK_ROOT /
# ANDROID_NDK_HOME plus `adb devices`.
# Pinned Android set: ndk;27.2.12479018, cmake;3.28.0, platform-tools,
# platforms;android-35 — installed via:
#   sdkmanager install "ndk;27.2.12479018" "cmake;3.28.0" "platform-tools" "platforms;android-35"

set -e
set -u

SELF_TEST=0

for arg in "$@"; do
  case "$arg" in
    --self-test) SELF_TEST=1 ;;
    -h|--help)
      echo "Usage: check-prerequisites.sh [--self-test]"
      exit 0
      ;;
    *)
      echo "check-prerequisites.sh: unexpected argument: $arg" >&2
      exit 2
      ;;
  esac
done

PINNED_INSTALL='sdkmanager install "ndk;27.2.12479018" "cmake;3.28.0" "platform-tools" "platforms;android-35"'
EXPECTED_CLI_VERSION='0.0.0'

jdk_fix() {
  echo "install a JDK 17+ and export JAVA_HOME, e.g. export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64"
}

sdk_fix() {
  echo "install the Android SDK and export ANDROID_HOME, e.g. export ANDROID_HOME=\$HOME/Android/Sdk && $PINNED_INSTALL"
}

ndk_fix() {
  echo "install the pinned NDK with sdkmanager \"ndk;27.2.12479018\" and export ANDROID_NDK_HOME, e.g. export ANDROID_NDK_HOME=\$ANDROID_HOME/ndk/27.2.12479018"
}

platform_fix() {
  echo "install the Android SDK platform with $PINNED_INSTALL"
}

cmake_fix() {
  echo "install CMake with $PINNED_INSTALL"
}

cli_fix() {
  echo "install the muse-native CLI version $EXPECTED_CLI_VERSION, then verify with muse-native --version (alias: muse --version)"
}
device_fix() {
  echo "connect a device or start an emulator, then verify with adb devices"
}

check_jdk() {
  if command -v java >/dev/null 2>&1 && command -v javac >/dev/null 2>&1 \
    && [ -n "${JAVA_HOME:-}" ] && [ -d "${JAVA_HOME:-}" ]; then
    echo "OK: JDK found (java/javac on PATH, JAVA_HOME=$JAVA_HOME)"
    return 0
  fi
  echo "MISSING: JDK is missing — $(jdk_fix)"
  return 1
}

check_sdk() {
  sdk_dir="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
  if [ -n "$sdk_dir" ] && [ -d "$sdk_dir" ] \
    && command -v sdkmanager >/dev/null 2>&1; then
    echo "OK: Android SDK found (ANDROID_HOME=$sdk_dir)"
    return 0
  fi
  echo "MISSING: Android SDK is missing — $(sdk_fix)"
  return 1
}

check_ndk() {
  if [ -n "${ANDROID_NDK_HOME:-}" ] && [ -d "${ANDROID_NDK_HOME:-}" ]; then
    echo "OK: NDK found (ANDROID_NDK_HOME=$ANDROID_NDK_HOME)"
    return 0
  fi
  echo "MISSING: NDK is missing — $(ndk_fix)"
  return 1
}

check_platform() {
  sdk_dir="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
  if [ -n "$sdk_dir" ] && [ -d "$sdk_dir/platforms/android-35" ]; then
    echo "OK: Android SDK platform android-35 found ($sdk_dir/platforms/android-35)"
    return 0
  fi
  echo "MISSING: Android SDK platform android-35 is missing — $(platform_fix)"
  return 1
}

check_cmake() {
  sdk_dir="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}"
  if [ -n "$sdk_dir" ] && [ -d "$sdk_dir/cmake/3.28.0" ]; then
    echo "OK: CMake 3.28.0 found ($sdk_dir/cmake/3.28.0)"
    return 0
  fi
  echo "MISSING: CMake 3.28.0 is missing — $(cmake_fix)"
  return 1
}

check_cli() {
  for cli in muse-native muse; do
    if command -v "$cli" >/dev/null 2>&1; then
      version="$("$cli" --version 2>/dev/null || true)"
      case "$version" in
        *"$EXPECTED_CLI_VERSION"*)
          echo "OK: muse-native CLI version $EXPECTED_CLI_VERSION found ($cli --version)"
          return 0
          ;;
      esac
    fi
  done
  echo "MISSING: muse-native CLI version $EXPECTED_CLI_VERSION is missing — $(cli_fix)"
  return 1
}

check_device() {
  if command -v adb >/dev/null 2>&1; then
    devices="$(adb devices 2>/dev/null || true)"
    if printf '%s\n' "$devices" | tail -n +2 | grep -qE '[[:alnum:]_.-]+[[:space:]]+device'; then
      echo "OK: device connected (adb devices reports a device)"
      return 0
    fi
  fi
  echo "MISSING: device is missing — no connected device, $(device_fix)"
  return 1
}

if [ "$SELF_TEST" -eq 1 ]; then
  echo "MISSING: JDK is missing — $(jdk_fix)"
  echo "MISSING: Android SDK is missing — $(sdk_fix)"
  echo "MISSING: Android SDK platform android-35 is missing — $(platform_fix)"
  echo "MISSING: NDK is missing — $(ndk_fix)"
  echo "MISSING: CMake 3.28.0 is missing — $(cmake_fix)"
  echo "MISSING: muse-native CLI version $EXPECTED_CLI_VERSION is missing — $(cli_fix)"
  echo "MISSING: device is missing — no connected device, $(device_fix)"
  exit 1
fi

failures=0
check_jdk || failures=$((failures + 1))
check_sdk || failures=$((failures + 1))
check_platform || failures=$((failures + 1))
check_ndk || failures=$((failures + 1))
check_cmake || failures=$((failures + 1))
check_cli || failures=$((failures + 1))
check_device || failures=$((failures + 1))

if [ "$failures" -ne 0 ]; then
  exit 1
fi
exit 0
