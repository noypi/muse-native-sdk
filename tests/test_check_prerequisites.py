"""Batch 2 (T5) — prerequisite check script with self-test (TC-SAMPLE-005).

Covers AC-SAMPLE-005/008 per specs-sample-project.md: a missing JDK,
Android SDK, NDK, or connected device produces an error naming the missing
prerequisite plus the command that installs/sets it, and exits non-zero.
`--self-test` simulates each missing item.

Hermetic: stdlib unittest only. Probes are satisfied through a fake
toolchain farm (fake `java`/`javac`/`adb`/`sdkmanager` on a prepended PATH
plus JAVA_HOME/ANDROID_HOME/ANDROID_NDK_HOME trees); absence is simulated
by removing farm entries from PATH/env. No real SDK, device, network,
secret, or config is touched. The device probe is driven through the fake
`adb`, which reports zero devices when FAKE_ADB_NO_DEVICES=1 (consumed by
the test-owned fake, not by the script's interface).

Expected state before implementation: RED — the script does not exist, so
every case fails with a missing-file/non-zero result.
"""

import os
import stat
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "scripts", "check-prerequisites.sh")

PINNED = (
    'ndk;27.2.12479018',
    'cmake;3.28.0',
    'platform-tools',
    'platforms;android-35',
)

PINNED_INSTALL_VERBATIM = (
    'android sdk install "ndk;27.2.12479018" "cmake;3.28.0" '
    '"platform-tools" "platforms;android-35"'
)

ITEMS = ("jdk", "android sdk", "ndk", "device")


def write_exe(path, body):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(body)
    mode = os.stat(path).st_mode
    os.chmod(path, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def make_farm(root):
    """Create fake toolchain farm; return (bin_dir, env)."""
    bin_dir = os.path.join(root, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    write_exe(os.path.join(bin_dir, "java"), "#!/bin/sh\necho fake-java\n")
    write_exe(os.path.join(bin_dir, "javac"), "#!/bin/sh\necho fake-javac\n")
    write_exe(
        os.path.join(bin_dir, "adb"),
        "#!/bin/sh\n"
        'if [ "${1:-}" = "devices" ]; then\n'
        '  if [ "${FAKE_ADB_NO_DEVICES:-0}" = "1" ]; then\n'
        '    echo "List of devices attached"\n'
        "  else\n"
        '    echo "List of devices attached"\n'
        '    echo "emulator-5554\\tdevice"\n'
        "  fi\n"
        "fi\n"
        "exit 0\n",
    )
    write_exe(
        os.path.join(bin_dir, "sdkmanager"),
        "#!/bin/sh\necho fake-sdkmanager\n",
    )
    write_exe(os.path.join(bin_dir, "android"), "#!/bin/sh\necho fake-android\n")
    write_exe(
        os.path.join(bin_dir, "muse-native"),
        "#!/bin/sh\necho muse-native 0.0.0\n",
    )
    java_home = os.path.join(root, "jdk")
    sdk_home = os.path.join(root, "sdk")
    ndk_home = os.path.join(sdk_home, "ndk", "27.2.12479018")
    os.makedirs(os.path.join(java_home, "bin"), exist_ok=True)
    os.makedirs(os.path.join(sdk_home, "platform-tools"), exist_ok=True)
    os.makedirs(os.path.join(ndk_home), exist_ok=True)
    os.makedirs(os.path.join(sdk_home, "platforms", "android-35"),
                exist_ok=True)
    os.makedirs(os.path.join(sdk_home, "cmake", "3.28.0"), exist_ok=True)
    write_exe(os.path.join(java_home, "bin", "java"), "#!/bin/sh\necho x\n")
    env = {
        "JAVA_HOME": java_home,
        "ANDROID_HOME": sdk_home,
        "ANDROID_SDK_ROOT": sdk_home,
        "ANDROID_NDK_HOME": ndk_home,
        "FAKE_ADB_NO_DEVICES": "0",
    }
    return bin_dir, env


def run_check(bin_dir, args, extra_env=None):
    env = dict(os.environ)
    for key in ("JAVA_HOME", "ANDROID_HOME", "ANDROID_SDK_ROOT",
                "ANDROID_NDK_HOME", "FAKE_ADB_NO_DEVICES"):
        env.pop(key, None)
    env["PATH"] = bin_dir + ":/usr/bin:/bin"
    env.update(extra_env or {})
    return subprocess.run(
        ["sh", SCRIPT] + list(args),
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
        env=env,
    )


class CheckPrerequisitesCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.bin, self.env = make_farm(self.tmp.name)


class TestCheckPrerequisitesScript(CheckPrerequisitesCase):
    def test_script_exists_and_is_executable(self):
        self.assertTrue(os.path.isfile(SCRIPT),
                        "scripts/check-prerequisites.sh must exist")
        self.assertTrue(os.access(SCRIPT, os.X_OK),
                        "scripts/check-prerequisites.sh must be executable")

    def test_script_shebang_and_strict_mode(self):
        with open(SCRIPT, encoding="utf-8") as fh:
            head = fh.read(4096)
        first = head.splitlines()[0] if head else ""
        self.assertTrue(first.startswith("#!"),
                        "script must start with a shebang line")
        self.assertIn("-e", head, "script must set -e")
        self.assertIn("-u", head, "script must set -u")

    def test_all_present_exits_zero_with_ok_lines(self):
        proc = run_check(self.bin, [], self.env)
        self.assertEqual(proc.returncode, 0,
                         "all faked prerequisites must pass:\n%s\n%s"
                         % (proc.stdout, proc.stderr))
        out = proc.stdout.lower()
        for item in ("jdk", "sdk", "ndk", "device"):
            self.assertIn(item, out,
                          "output must report one line per prerequisite")
        self.assertGreaterEqual(
            sum(1 for line in proc.stdout.splitlines()
                if line.strip().upper().startswith("OK")),
            4, "every satisfied prerequisite must print an OK line")

    def test_missing_jdk_names_item_fix_and_fails(self):
        env = dict(self.env)
        env.pop("JAVA_HOME", None)
        empty = os.path.join(self.tmp.name, "emptybin")
        os.makedirs(empty, exist_ok=True)
        proc = run_check(empty, [], env)
        self.assertNotEqual(proc.returncode, 0)
        out = (proc.stdout + proc.stderr).lower()
        self.assertTrue("jdk" in out or "java" in out,
                        "missing JDK must be named:\n%s" % out)
        self.assertTrue("install" in out or "set" in out or "export" in out,
                        "missing JDK must print a fix command:\n%s" % out)

    def test_missing_sdk_names_item_and_fails(self):
        env = dict(self.env)
        for key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
            env.pop(key, None)
        empty = os.path.join(self.tmp.name, "emptybin2")
        os.makedirs(empty, exist_ok=True)
        proc = run_check(empty, [], env)
        self.assertNotEqual(proc.returncode, 0)
        out = (proc.stdout + proc.stderr).lower()
        self.assertTrue("sdk" in out,
                        "missing Android SDK must be named:\n%s" % out)

    def test_no_device_names_device_and_fails(self):
        env = dict(self.env)
        env["FAKE_ADB_NO_DEVICES"] = "1"
        proc = run_check(self.bin, [], env)
        self.assertNotEqual(proc.returncode, 0,
                            "zero attached devices must fail")
        out = (proc.stdout + proc.stderr).lower()
        self.assertIn("device", out,
                      "missing device must be named:\n%s" % out)

    def test_self_test_simulates_every_missing_item(self):
        proc = run_check(self.bin, ["--self-test"], self.env)
        self.assertNotEqual(proc.returncode, 0,
                            "--self-test must exit non-zero")
        out = (proc.stdout + proc.stderr).lower()
        for item in ITEMS:
            self.assertIn(item, out,
                          "--self-test must simulate missing %s:\n%s"
                          % (item, out))

    def test_missing_lines_carry_fix_commands(self):
        proc = run_check(self.bin, ["--self-test"], self.env)
        missing = [line for line in
                   (proc.stdout + proc.stderr).splitlines()
                   if "missing" in line.lower()]
        self.assertGreaterEqual(len(missing), 4,
                                "--self-test must print one MISSING line "
                                "per prerequisite")
        for line in missing:
            self.assertTrue(
                "install" in line.lower() or "export" in line.lower()
                or "adb" in line.lower(),
                "MISSING line must carry a fix command: %s" % line)

    def test_pinned_android_set_is_reported(self):
        with open(SCRIPT, encoding="utf-8") as fh:
            body = fh.read()
        for pin in PINNED:
            self.assertIn(pin, body,
                          "script must print the pinned install set %s" % pin)
        proc = run_check(self.bin, ["--self-test"], self.env)
        combined = proc.stdout + proc.stderr
        self.assertTrue(
            any(pin in combined for pin in PINNED),
            "--self-test output must include the pinned install command")


if __name__ == "__main__":
    unittest.main()


# ---------------------------------------------------------------------------
# Batch 1 (bug-build-008: T1, T2) — android CLI migration.
#
# Hermetic: stdlib unittest only. Shell behavior is exercised through
# function-level sourcing of scripts/check-prerequisites.sh (function
# definitions only; the script main body is stripped before sourcing) with
# a fake `android` CLI on a prepended PATH plus temp SDK trees.
# doctor-checks.js is exercised through `node -e require(...)` in-process
# JSON output plus source-text assertions. No network, secret, config, or
# live SDK is touched.
# ---------------------------------------------------------------------------

import json as _json
import shutil as _shutil

DOCTOR_CHECKS = os.path.join(
    REPO_ROOT, "tools", "cli", "lib", "doctor-checks.js")

ANDROID_PINNED = ("ndk;27.2.12479018", "platforms;android-35")


def _source_functions_body():
    """Return script body with the executable main tail removed."""
    with open(SCRIPT, encoding="utf-8") as fh:
        body = fh.read()
    marker = 'if [ "$SELF_TEST" -eq 1 ]; then'
    idx = body.find(marker)
    if idx == -1:
        raise AssertionError("check-prerequisites.sh main guard not found")
    return body[:idx]


def run_shell_func(func, bin_dir, extra_env):
    """Source check-prerequisites.sh functions and call `func`; return proc."""
    funcs = _source_functions_body()
    with tempfile.NamedTemporaryFile("w", suffix=".sh", delete=False,
                                     encoding="utf-8") as fh:
        fh.write(funcs)
        fh.write('\n%s\n' % func)
        harness = fh.name
    env = dict(os.environ)
    for key in ("JAVA_HOME", "ANDROID_HOME", "ANDROID_SDK_ROOT",
                "ANDROID_NDK_HOME", "FAKE_ADB_NO_DEVICES"):
        env.pop(key, None)
    env["PATH"] = bin_dir + ":/usr/bin:/bin"
    env.update(extra_env or {})
    try:
        return subprocess.run(
            ["sh", harness],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=60,
            env=env,
        )
    finally:
        os.unlink(harness)


def make_android_only_farm(root):
    """Farm variant: fake `android` CLI only (no sdkmanager)."""
    bin_dir, env = make_farm(root)
    # This variant's contract is a cmake-absent farm (see
    # test_cmake_absent_reports_missing_with_fix), so strip the cmake
    # tree that the refreshed make_farm now provides for the
    # all-present path.
    _shutil.rmtree(os.path.join(env["ANDROID_HOME"], "cmake"),
                   ignore_errors=True)
    solo = os.path.join(root, "androidonly")
    os.makedirs(solo, exist_ok=True)
    for name in os.listdir(bin_dir):
        if name == "sdkmanager":
            continue
        _shutil.copy(os.path.join(bin_dir, name), os.path.join(solo, name))
    write_exe(os.path.join(solo, "android"), "#!/bin/sh\necho fake-android\n")
    # Belt-and-braces: ensure no sdkmanager leaks onto PATH entries used.
    stray = os.path.join(solo, "sdkmanager")
    if os.path.exists(stray):
        os.unlink(stray)
    return solo, env


def doctor_checks_json():
    node = _shutil.which("node")
    if node is None:
        raise unittest.SkipTest("node is not installed")
    proc = subprocess.run(
        [node, "-e",
         "const m=require('./tools/cli/lib/doctor-checks.js');"
         "console.log(JSON.stringify(m.CHECKS))"],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )
    if proc.returncode != 0:
        raise AssertionError("cannot load doctor-checks.js CHECKS:\n%s\n%s"
                             % (proc.stdout, proc.stderr))
    return _json.loads(proc.stdout)


class TestBatch1T1AndroidCli(CheckPrerequisitesCase):
    """T1: check-prerequisites.sh android CLI with accept-both probe."""

    def test_pinned_install_verbatim(self):
        with open(SCRIPT, encoding="utf-8") as fh:
            body = fh.read()
        self.assertIn("PINNED_INSTALL='%s'" % PINNED_INSTALL_VERBATIM,
                      body,
                      "PINNED_INSTALL must carry the verbatim android "
                      "sdk install line")

    def test_fix_functions_consume_pinned_install(self):
        with open(SCRIPT, encoding="utf-8") as fh:
            body = fh.read()
        for func in ("sdk_fix", "ndk_fix", "platform_fix", "cmake_fix"):
            self.assertIn(func, body)
        self.assertNotIn("sdkmanager", body,
                         "no fix text may retain the bare sdkmanager spelling")

    def test_check_sdk_accepts_android_only(self):
        solo, env = make_android_only_farm(self.tmp.name)
        proc = run_shell_func("check_sdk", solo, env)
        self.assertEqual(proc.returncode, 0,
                         "android-only farm must pass check_sdk:\n%s\n%s"
                         % (proc.stdout, proc.stderr))
        self.assertIn("OK: Android SDK", proc.stdout)

    def test_check_sdk_still_accepts_legacy_sdkmanager(self):
        proc = run_shell_func("check_sdk", self.bin, self.env)
        self.assertEqual(proc.returncode, 0,
                         "legacy sdkmanager farm must keep passing:\n%s\n%s"
                         % (proc.stdout, proc.stderr))
        self.assertIn("OK: Android SDK", proc.stdout)

    def test_check_sdk_fails_with_no_cli_but_dir_present(self):
        empty = os.path.join(self.tmp.name, "nocli")
        os.makedirs(empty, exist_ok=True)
        proc = run_shell_func("check_sdk", empty, self.env)
        self.assertNotEqual(proc.returncode, 0,
                            "SDK dir without any CLI must fail check_sdk")
        out = proc.stdout + proc.stderr
        self.assertIn("MISSING: Android SDK is missing", out)
        self.assertIn("android sdk install", out)

    def test_check_sdk_missing_names_android_fix(self):
        empty = os.path.join(self.tmp.name, "nocli2")
        os.makedirs(empty, exist_ok=True)
        env = dict(self.env)
        env.pop("ANDROID_HOME", None)
        env.pop("ANDROID_SDK_ROOT", None)
        proc = run_shell_func("check_sdk", empty, env)
        self.assertNotEqual(proc.returncode, 0)
        out = proc.stdout + proc.stderr
        self.assertIn("MISSING: Android SDK is missing", out)
        self.assertIn("android sdk install", out)

    def test_self_test_missing_output_uses_android_spelling(self):
        proc = run_check(self.bin, ["--self-test"], self.env)
        combined = proc.stdout + proc.stderr
        self.assertIn("android sdk install", combined)
        self.assertNotIn("sdkmanager", combined,
                         "--self-test MISSING output must not name sdkmanager")

    def test_cmake_absent_reports_missing_with_fix(self):
        solo, env = make_android_only_farm(self.tmp.name)
        sdk = env["ANDROID_HOME"]
        cmake_dir = os.path.join(sdk, "cmake", "3.28.0")
        self.assertFalse(os.path.exists(cmake_dir),
                         "farm must leave cmake/3.28.0 absent")
        proc = run_shell_func("check_cmake", solo, env)
        self.assertNotEqual(proc.returncode, 0,
                            "absent cmake/3.28.0 must report MISSING")
        out = proc.stdout + proc.stderr
        self.assertIn("MISSING: CMake 3.28.0 is missing", out)
        self.assertIn('android sdk install', out)
        self.assertIn('"cmake;3.28.0"', out)


class TestBatch1T2DoctorChecks(unittest.TestCase):
    """T2: doctor-checks.js Android fixes pinned to the android CLI."""

    def test_no_sdkmanager_spelling(self):
        with open(DOCTOR_CHECKS, encoding="utf-8") as fh:
            body = fh.read()
        self.assertNotIn("sdkmanager", body,
                         "doctor-checks.js must not reference sdkmanager")

    def test_node_check_clean(self):
        node = _shutil.which("node")
        if node is None:
            raise unittest.SkipTest("node is not installed")
        proc = subprocess.run(
            [node, "--check", DOCTOR_CHECKS],
            capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(proc.returncode, 0,
                         "node --check must pass:\n%s\n%s"
                         % (proc.stdout, proc.stderr))

    def test_android_fixes_pinned(self):
        checks = {c["name"]: c for c in doctor_checks_json()}
        for name in ("ndk", "sdk", "platform-tools", "emulator"):
            self.assertIn(name, checks, "CHECKS must keep entry %s" % name)
            fix = checks[name]["fix"]
            self.assertTrue(
                fix.startswith("android sdk install"),
                "%s fix must start with `android sdk install`: %r"
                % (name, fix))
        self.assertIn("ndk;27.2.12479018", checks["ndk"]["fix"])
        self.assertIn("platforms;android-35", checks["sdk"]["fix"])
        for entry in checks.values():
            for key in ("name", "envVar", "subsystem", "label", "fix"):
                self.assertIn(key, entry,
                              "every CHECKS entry must expose %s" % key)

    def test_licenses_fix_either_form(self):
        # ASSUMPTION (plan A2 ambiguous): coder may land either the
        # `android sdk`-spelling license fix or the $ANDROID_HOME/licenses
        # file-existence fix; this test accepts either documented form.
        checks = {c["name"]: c for c in doctor_checks_json()}
        self.assertIn("licenses", checks)
        fix = checks["licenses"]["fix"]
        android_spelling = fix.startswith("android sdk") and (
            "license" in fix.lower())
        file_existence = ("licenses" in fix
                          and ("ANDROID_HOME" in fix
                               or "licenses/" in fix
                               or "test -d" in fix
                               or "ls " in fix))
        self.assertTrue(android_spelling or file_existence,
                        "licenses fix must be either an `android sdk` "
                        "license form or an $ANDROID_HOME/licenses "
                        "file-existence form: %r" % (fix,))
