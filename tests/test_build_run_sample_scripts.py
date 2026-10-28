"""Batch 2 (T4) — build/run sample scripts: delegate to muse-native only (TC-SAMPLE-001/004).

Covers AC-SAMPLE-004 per specs-sample-project.md: `scripts/build-sample.sh`
and `scripts/run-sample.sh` compile and launch `examples/m3-gallery` via
`muse-native build/run` with no manual Gradle/CMake/adb invocation.

Hermetic: stdlib unittest only. A fake `muse-native` on a prepended PATH
records its argv and fakes success/failure; no SDK, device, network,
secrets, or config is touched. The prereq gate (if the scripts call
`scripts/check-prerequisites.sh` first per the T5 plan detail) is satisfied
by the shared fake toolchain farm — see `fake_toolchain_farm()` and the
ASSUMPTION in T4.md.

Expected state before implementation: RED — neither script exists, so every
case fails with a missing-file/non-zero result.
"""

import os
import re
import stat
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_SCRIPT = os.path.join(REPO_ROOT, "scripts", "build-sample.sh")
RUN_SCRIPT = os.path.join(REPO_ROOT, "scripts", "run-sample.sh")
DEFAULT_TARGET = "examples/m3-gallery"

FORBIDDEN_TOKENS = ("gradle", "cmake", "adb", "sdkmanager", "bazel", "ninja")


def write_exe(path, body):
    """Write an executable shell file; return its path."""
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(body)
    mode = os.stat(path).st_mode
    os.chmod(path, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def fake_muse_native(bin_dir, log_path, exit_code=0, marker="FAKE-MUSE-NATIVE"):
    """Install a fake `muse-native` recording argv lines to log_path."""
    return write_exe(
        os.path.join(bin_dir, "muse-native"),
        "#!/bin/sh\n"
        'echo "$@" >> "%s"\n' % log_path
        + 'echo "%s $@"\n' % marker
        + "exit %d\n" % exit_code,
    )


def fake_toolchain_farm(root):
    """Build a fake toolchain satisfying PATH/env-based prereq probes.

    Provides fake `java`, `javac`, `adb` (reports one attached device for
    `adb devices`), `sdkmanager`, plus JAVA_HOME/ANDROID_HOME/ANDROID_NDK_HOME
    trees. Returns (bin_dir, env_overrides).
    """
    bin_dir = os.path.join(root, "bin")
    os.makedirs(bin_dir, exist_ok=True)
    write_exe(os.path.join(bin_dir, "java"), "#!/bin/sh\necho fake-java\n")
    write_exe(os.path.join(bin_dir, "javac"), "#!/bin/sh\necho fake-javac\n")
    write_exe(
        os.path.join(bin_dir, "adb"),
        "#!/bin/sh\n"
        'if [ "${1:-}" = "devices" ]; then\n'
        '  echo "List of devices attached"\n'
        '  echo "emulator-5554\\tdevice"\n'
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
    write_exe(
        os.path.join(sdk_home, "platform-tools", "adb"),
        "#!/bin/sh\nexit 0\n",
    )
    env = {
        "JAVA_HOME": java_home,
        "ANDROID_HOME": sdk_home,
        "ANDROID_SDK_ROOT": sdk_home,
        "ANDROID_NDK_HOME": ndk_home,
    }
    return bin_dir, env


def run_script(script, args, bin_dir, extra_env=None):
    """Run a script hermetically with bin_dir prepended to PATH."""
    env = dict(os.environ)
    env["PATH"] = bin_dir + ":/usr/bin:/bin"
    for key in ("JAVA_HOME", "ANDROID_HOME", "ANDROID_SDK_ROOT",
                "ANDROID_NDK_HOME"):
        env.pop(key, None)
    env.update(extra_env or {})
    return subprocess.run(
        ["sh", script] + list(args),
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
        env=env,
    )


def read_log(log_path):
    with open(log_path, encoding="utf-8") as fh:
        return fh.read()


class SampleScriptCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.bin = os.path.join(self.tmp.name, "fakebin")
        os.makedirs(self.bin, exist_ok=True)
        self.log = os.path.join(self.tmp.name, "muse-native.log")
        open(self.log, "w", encoding="utf-8").close()
        farm_bin, self.farm_env = fake_toolchain_farm(
            os.path.join(self.tmp.name, "farm"))
        for name in os.listdir(farm_bin):
            src = os.path.join(farm_bin, name)
            dst = os.path.join(self.bin, name)
            if not os.path.exists(dst):
                with open(src, "rb") as fh_in, open(dst, "wb") as fh_out:
                    fh_out.write(fh_in.read())
                os.chmod(dst, 0o755)
        fake_muse_native(self.bin, self.log)

    def run_case(self, script, args):
        return run_script(script, args, self.bin, self.farm_env)


class TestBuildSampleScript(SampleScriptCase):
    def test_build_script_exists_and_is_executable(self):
        self.assertTrue(os.path.isfile(BUILD_SCRIPT),
                        "scripts/build-sample.sh must exist")
        self.assertTrue(os.access(BUILD_SCRIPT, os.X_OK),
                        "scripts/build-sample.sh must be executable")

    def test_build_script_shebang_and_strict_mode(self):
        with open(BUILD_SCRIPT, encoding="utf-8") as fh:
            head = fh.read(4096)
        first = head.splitlines()[0] if head else ""
        self.assertTrue(first.startswith("#!"),
                        "script must start with a shebang line")
        self.assertIn("set ", head)
        self.assertIn("-e", head, "script must set -e")
        self.assertIn("-u", head, "script must set -u")

    def test_build_defaults_to_gallery_target(self):
        proc = self.run_case(BUILD_SCRIPT, [])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("build %s" % DEFAULT_TARGET, read_log(self.log))

    def test_build_passes_explicit_target(self):
        proc = self.run_case(BUILD_SCRIPT, ["examples/flappy"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("build examples/flappy", read_log(self.log))

    def test_build_propagates_cli_failure(self):
        fake_muse_native(self.bin, self.log, exit_code=3)
        proc = self.run_case(BUILD_SCRIPT, [])
        self.assertNotEqual(proc.returncode, 0,
                            "CLI failure must propagate a non-zero exit")

    def test_build_invokes_no_manual_toolchain(self):
        with open(BUILD_SCRIPT, encoding="utf-8") as fh:
            lines = fh.readlines()
        code = "\n".join(
            line for line in lines
            if not line.lstrip().startswith("#")
            and not re.match(r"\s*echo\b", line))
        for token in FORBIDDEN_TOKENS:
            self.assertNotIn(token, code.lower(),
                             "build script must not invoke %s" % token)


class TestRunSampleScript(SampleScriptCase):
    def test_run_script_exists_and_is_executable(self):
        self.assertTrue(os.path.isfile(RUN_SCRIPT),
                        "scripts/run-sample.sh must exist")
        self.assertTrue(os.access(RUN_SCRIPT, os.X_OK),
                        "scripts/run-sample.sh must be executable")

    def test_run_script_shebang_and_strict_mode(self):
        with open(RUN_SCRIPT, encoding="utf-8") as fh:
            head = fh.read(4096)
        first = head.splitlines()[0] if head else ""
        self.assertTrue(first.startswith("#!"),
                        "script must start with a shebang line")
        self.assertIn("-e", head, "script must set -e")
        self.assertIn("-u", head, "script must set -u")

    def test_run_defaults_to_gallery_target(self):
        proc = self.run_case(RUN_SCRIPT, [])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("run %s" % DEFAULT_TARGET, read_log(self.log))

    def test_run_passes_explicit_target(self):
        proc = self.run_case(RUN_SCRIPT, ["examples/muse-demo"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("run examples/muse-demo", read_log(self.log))

    def test_run_propagates_cli_failure(self):
        fake_muse_native(self.bin, self.log, exit_code=3)
        proc = self.run_case(RUN_SCRIPT, [])
        self.assertNotEqual(proc.returncode, 0,
                            "CLI failure must propagate a non-zero exit")

    def test_run_invokes_no_manual_toolchain(self):
        with open(RUN_SCRIPT, encoding="utf-8") as fh:
            lines = fh.readlines()
        code = "\n".join(
            line for line in lines
            if not line.lstrip().startswith("#")
            and not re.match(r"\s*echo\b", line))
        for token in FORBIDDEN_TOKENS:
            self.assertNotIn(token, code.lower(),
                             "run script must not invoke %s" % token)


if __name__ == "__main__":
    unittest.main()
