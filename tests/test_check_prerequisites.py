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
    java_home = os.path.join(root, "jdk")
    sdk_home = os.path.join(root, "sdk")
    ndk_home = os.path.join(sdk_home, "ndk", "27.2.12479018")
    os.makedirs(os.path.join(java_home, "bin"), exist_ok=True)
    os.makedirs(os.path.join(sdk_home, "platform-tools"), exist_ok=True)
    os.makedirs(os.path.join(ndk_home), exist_ok=True)
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
