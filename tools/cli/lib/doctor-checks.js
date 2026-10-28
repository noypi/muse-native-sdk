'use strict';
// T2 — FR-DX-008 / AC-DX-033 / TC-DX-033.
// Check catalogue for the doctor command. Each entry maps one of the eight
// required categories to its env-style probe input, owning subsystem, and
// exact fix command. Hermetic: probes read process.env only (MUSE_*),
// never real SDK installs, devices, or secrets.

const CHECKS = [
  {
    name: 'ndk',
    envVar: 'MUSE_NDK_HOME',
    subsystem: 'toolchain/ndk',
    label: 'Android NDK',
    fix: 'sdkmanager "ndk;25.1.8937393"',
  },
  {
    name: 'sdk',
    envVar: 'MUSE_SDK_HOME',
    subsystem: 'toolchain/sdk',
    label: 'Android SDK',
    fix: 'sdkmanager --install "platforms;android-34"',
  },
  {
    name: 'platform-tools',
    envVar: 'MUSE_PLATFORM_TOOLS',
    subsystem: 'toolchain/platform-tools',
    label: 'Android platform-tools',
    fix: 'sdkmanager "platform-tools"',
  },
  {
    name: 'emulator',
    envVar: 'MUSE_EMULATOR',
    subsystem: 'toolchain/emulator',
    label: 'Android emulator',
    fix: 'sdkmanager "emulator"',
  },
  {
    name: 'licenses',
    envVar: 'MUSE_LICENSES',
    subsystem: 'toolchain/licenses',
    label: 'Android SDK licenses',
    fix: 'yes | sdkmanager --licenses',
  },
  {
    name: 'jdk',
    envVar: 'MUSE_JDK_HOME',
    subsystem: 'toolchain/jdk',
    label: 'JDK 17',
    fix: 'sudo apt install temurin-17-jdk',
  },
  {
    name: 'device',
    envVar: 'MUSE_DEVICE',
    subsystem: 'device/connectivity',
    label: 'device connectivity',
    fix: 'adb devices',
  },
  {
    name: 'driver',
    envVar: 'MUSE_DRIVER',
    subsystem: 'driver/capability',
    label: 'device driver',
    fix: 'muse driver --install android',
  },
];

module.exports = { CHECKS };
