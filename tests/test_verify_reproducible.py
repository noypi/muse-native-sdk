"""Batch 2 (T6) — verify-reproducible mode of scripts/build-sample.sh (TC-SAMPLE-006).

Covers AC-SAMPLE-006 per specs-sample-project.md:
`scripts/build-sample.sh --verify-reproducible [target]` performs two clean
builds of the same commit, byte-compares artifacts, prints pinned
toolchain versions, and exits non-zero with a diff path on mismatch.

Hermetic: stdlib unittest only. A fake `muse-native` on a prepended PATH
fakes each build by writing an artifact file; nondeterminism between the
two builds is toggled by FAKE_MUSE_BUILD_FLIP=1, which is consumed by the
test-owned fake (not by the script's interface). No Gradle, SDK, device,
network, secret, or config is touched.

Expected state before implementation: RED — the script/flag does not exist,
so every case fails.
"""

import os
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "scripts", "build-sample.sh")
DEFAULT_TARGET = "examples/m3-gallery"

PINNED_VERSION_TOKENS = ("27.2.12479018", "3.28.0", "android-35")


def write_exe(path, body):
    import stat
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(body)
    mode = os.stat(path).st_mode
    os.chmod(path, mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


def install_fake_muse_native(bin_dir, artifact_dir, log_path):
    """Fake `muse-native build` writing a deterministic artifact per build.

    Each `build` invocation appends its argv to the log and writes
    artifact.bin; when FAKE_MUSE_BUILD_FLIP=1 the second and later builds
    write different bytes to simulate a non-reproducible toolchain.
    """
    return write_exe(
        os.path.join(bin_dir, "muse-native"),
        "#!/bin/sh\n"
        "LOG=\"%s\"\n"
        "OUT=\"%s\"\n" % (log_path, os.path.join(artifact_dir, "artifact.bin"))
        + 'echo "$@" >> "$LOG"\n'
        + "COUNT=$(wc -l < \"$LOG\" | tr -d ' ')\n"
        + "CONTENT=\"deterministic-artifact-bytes\"\n"
        + 'if [ "${FAKE_MUSE_BUILD_FLIP:-0}" = "1" ] && [ "$COUNT" -ge 2 ]; then\n'
        + "  CONTENT=\"nondeterministic-artifact-bytes-$COUNT\"\n"
        + "fi\n"
        + 'printf "%s" "$CONTENT" > "$OUT"\n'
        + 'echo "fake build $COUNT"\n'
        + "exit 0\n",
    )


def run_verify(bin_dir, args, extra_env=None):
    env = dict(os.environ)
    env["PATH"] = bin_dir + ":/usr/bin:/bin"
    for key in ("JAVA_HOME", "ANDROID_HOME", "ANDROID_SDK_ROOT",
                "ANDROID_NDK_HOME"):
        env.pop(key, None)
    env.update(extra_env or {})
    return subprocess.run(
        ["sh", SCRIPT] + list(args),
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=120,
        env=env,
    )


class VerifyReproducibleCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.bin = os.path.join(self.tmp.name, "fakebin")
        os.makedirs(self.bin, exist_ok=True)
        self.artifacts = os.path.join(self.tmp.name, "artifacts")
        os.makedirs(self.artifacts, exist_ok=True)
        self.log = os.path.join(self.tmp.name, "builds.log")
        open(self.log, "w", encoding="utf-8").close()
        install_fake_muse_native(self.bin, self.artifacts, self.log)


class TestVerifyReproducible(VerifyReproducibleCase):
    def test_identical_builds_exit_zero(self):
        proc = run_verify(self.bin, ["--verify-reproducible"])
        self.assertEqual(proc.returncode, 0,
                         "byte-identical builds must exit 0:\n%s\n%s"
                         % (proc.stdout, proc.stderr))

    def test_two_clean_builds_are_performed(self):
        proc = run_verify(self.bin, ["--verify-reproducible"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        with open(self.log, encoding="utf-8") as fh:
            invocations = [line for line in fh.read().splitlines() if line]
        builds = [line for line in invocations if line.startswith("build")]
        self.assertGreaterEqual(
            len(builds), 2,
            "verify mode must perform two clean builds, saw: %r" % invocations)

    def test_verify_reports_pinned_toolchain_versions(self):
        proc = run_verify(self.bin, ["--verify-reproducible"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        combined = proc.stdout + proc.stderr
        for token in PINNED_VERSION_TOKENS:
            self.assertIn(token, combined,
                          "verify output must report pinned version %s" % token)

    def test_verify_defaults_to_gallery_target(self):
        proc = run_verify(self.bin, ["--verify-reproducible"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        with open(self.log, encoding="utf-8") as fh:
            logged = fh.read()
        self.assertIn(DEFAULT_TARGET, logged,
                      "verify must default to %s" % DEFAULT_TARGET)

    def test_verify_accepts_explicit_target(self):
        proc = run_verify(self.bin,
                          ["--verify-reproducible", "examples/flappy"])
        self.assertEqual(proc.returncode, 0, proc.stderr)
        with open(self.log, encoding="utf-8") as fh:
            logged = fh.read()
        self.assertIn("examples/flappy", logged)

    def test_mismatch_exits_nonzero_with_diff_path(self):
        proc = run_verify(self.bin, ["--verify-reproducible"],
                          {"FAKE_MUSE_BUILD_FLIP": "1"})
        self.assertNotEqual(proc.returncode, 0,
                            "divergent builds must exit non-zero")
        combined = proc.stdout + proc.stderr
        self.assertTrue("diff" in combined.lower()
                        or ".diff" in combined
                        or "mismatch" in combined.lower()
                        or "differ" in combined.lower(),
                        "mismatch output must name a diff path:\n%s" % combined)


if __name__ == "__main__":
    unittest.main()
