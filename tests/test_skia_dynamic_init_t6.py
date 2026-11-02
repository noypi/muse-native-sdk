"""Batch 3 (T6) — Executed dynamic-initializer scan.

Covers FR-DEPS-011 / AC-DEPS-033 per specs-cpp-dependencies.md: the
startup-path dynamic-initializer scan via
``tools/deps/dynamic_init_check.sh`` is clean and its outcome is recorded
in the manifest as ``dynamic_init_clean``; any hit names the offending
translation unit and fails the CORE-012 check (TC-DEPS-033).

Hermetic: stdlib unittest only. CLI cases drive the committed shell script
with tmp fixtures via subprocess; manifest cases read the committed JSON
read-only; gate cases call ``tools.deps.admission_check.find_failures``
in-process on tmp entries. No downloads, no secrets, no services.
Expected state before implementation: RED — the committed manifest's Skia
Graphite row carries no ``dynamic_init_clean`` verdict (filled by the
executed scan only), so the manifest-presence tests fail; only the
CLI-format and negative/gate cases pass.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "dynamic_init_check.sh")
MANIFEST = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")

SKIA_NAME = "Skia Graphite"

sys.path.insert(0, REPO_ROOT)
from tools.deps.admission_check import CHECK_DYNAMIC_INIT, find_failures  # noqa: E402


def run_cli(*args, extra_env=None):
    """Run dynamic_init_check.sh hermetically; return CompletedProcess."""
    env = dict(os.environ)
    if extra_env:
        env.update(extra_env)
    return subprocess.run(
        [SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
        env=env,
    )


def load_manifest(path=MANIFEST):
    """Load a manifest; accept {dependencies:[...]} or a bare list."""
    with open(path, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    if isinstance(data, dict):
        return data.get("dependencies", [])
    return data


def find_skia(entries, name=SKIA_NAME):
    """Return the Skia Graphite row or None when absent."""
    for entry in entries:
        if isinstance(entry, dict) and entry.get("name") == name:
            return entry
    return None


def good_entry(name, **overrides):
    """A manifest entry carrying every field the admission gate requires."""
    data = {
        "name": name,
        "owner": "graphics",
        "interface": "GraphiteRender",
        "exit_plan": "CPU rasterizer fallback",
        "licence": "BSD-3-Clause",
        "link": "static",
        "size_delta_bytes": 120,
        "dynamic_init_clean": True,
        "fno_exceptions": True,
        "version": "m158",
    }
    data.update(overrides)
    return data


class TestDynamicInitCli(unittest.TestCase):
    """AC-DEPS-033: the scan runs clean or names the offending TU."""

    def test_clean_object_passes(self):
        """A fixture with no initializer evidence exits 0."""
        with tempfile.TemporaryDirectory() as tmp:
            clean = os.path.join(tmp, "clean.o")
            with open(clean, "w", encoding="utf-8") as handle:
                handle.write("clean object with no initializers\n.text nop nop\n")
            proc = run_cli(clean)
        self.assertEqual(proc.returncode, 0, proc.stdout + proc.stderr)
        self.assertNotIn("CORE-012", proc.stdout + proc.stderr)

    def test_initializer_hit_names_object_and_core012(self):
        """A .init_array fixture exits 1 naming object + evidence + CORE-012."""
        with tempfile.TemporaryDirectory() as tmp:
            bad = os.path.join(tmp, "init.o")
            with open(bad, "w", encoding="utf-8") as handle:
                handle.write("object with .init_array DT_INIT_ARRAY entry\n")
            proc = run_cli(bad)
        self.assertEqual(proc.returncode, 1)
        self.assertIn(bad, proc.stdout)
        self.assertIn("CORE-012", proc.stdout)

    def test_missing_input_fails_closed(self):
        """A missing object file exits 1 (never silently passes)."""
        with tempfile.TemporaryDirectory() as tmp:
            proc = run_cli(os.path.join(tmp, "does-not-exist.o"))
        self.assertEqual(proc.returncode, 1)

    def test_missing_manifest_fails_closed(self):
        """An unreadable --manifest path exits 1."""
        with tempfile.TemporaryDirectory() as tmp:
            clean = os.path.join(tmp, "clean.o")
            with open(clean, "w", encoding="utf-8") as handle:
                handle.write("clean object with no initializers\n")
            proc = run_cli(
                "--manifest", os.path.join(tmp, "no-manifest.json"), clean
            )
        self.assertEqual(proc.returncode, 1)

    def test_missing_probe_tool_fails_closed(self):
        """No readelf/llvm-readobj on PATH exits 1 naming the reason."""
        with tempfile.TemporaryDirectory() as tmp:
            clean = os.path.join(tmp, "clean.o")
            with open(clean, "w", encoding="utf-8") as handle:
                handle.write("clean object with no initializers\n")
            stub = os.path.join(tmp, "empty-path")
            os.mkdir(stub)
            proc = run_cli(
                clean,
                extra_env={"PATH": stub, "DYNAMIC_INIT_CHECK_PROBE_PATH": stub},
            )
        self.assertEqual(proc.returncode, 1)
        combined = proc.stdout + proc.stderr
        self.assertTrue(
            "readelf" in combined or "llvm-readobj" in combined
            or "probe" in combined,
            "must name the missing tool/reason: %r" % combined,
        )


class TestSkiaDynamicInitManifest(unittest.TestCase):
    """AC-DEPS-033: scan outcome recorded as manifest dynamic_init_clean."""

    def test_skia_dynamic_init_clean_recorded(self):
        """Skia Graphite row records dynamic_init_clean=True (executed)."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        self.assertIs(
            row.get("dynamic_init_clean"), True,
            "dynamic_init_clean unmeasured — execute the scan (AC-DEPS-033)",
        )


class TestSkiaDynamicInitGate(unittest.TestCase):
    """AC-DEPS-030/033: unclean/missing verdict denies under dynamic-init."""

    def checks_for(self, entry):
        """Failed check names for a single manifest entry."""
        return [check for _, check in find_failures([entry])]

    def test_clean_verdict_admits_dynamic_init(self):
        """dynamic_init_clean=True passes the dynamic-init check."""
        self.assertNotIn(
            CHECK_DYNAMIC_INIT, self.checks_for(good_entry(SKIA_NAME))
        )

    def test_false_verdict_denied(self):
        """dynamic_init_clean=False denies naming the TU path (fail closed)."""
        entry = good_entry(SKIA_NAME, dynamic_init_clean=False)
        failures = find_failures([entry])
        self.assertIn((SKIA_NAME, CHECK_DYNAMIC_INIT), failures)

    def test_missing_verdict_denied(self):
        """No dynamic_init_clean key denies under dynamic-init."""
        entry = good_entry(SKIA_NAME)
        del entry["dynamic_init_clean"]
        self.assertIn(CHECK_DYNAMIC_INIT, self.checks_for(entry))

    def test_truthy_non_true_denied(self):
        """'true'/1 are not a clean verdict — only True admits (boundary)."""
        for wrong in ("true", 1, "yes"):
            with self.subTest(value=wrong):
                self.assertIn(
                    CHECK_DYNAMIC_INIT,
                    self.checks_for(
                        good_entry(SKIA_NAME, dynamic_init_clean=wrong)
                    ),
                )

    def test_truthy_non_true_extended_denied(self):
        """Lock: True-adjacent values ('True',1.0,[True],2) never admit."""
        for wrong in ("True", "TRUE", "1", 1.0, 2, [True], {"v": True}):
            with self.subTest(value=wrong):
                self.assertIn(
                    CHECK_DYNAMIC_INIT,
                    self.checks_for(
                        good_entry(SKIA_NAME, dynamic_init_clean=wrong)
                    ),
                )

    def test_only_dynamic_init_fails_in_isolation(self):
        """A False verdict fails exactly one check (no cascading)."""
        entry = good_entry(SKIA_NAME, dynamic_init_clean=False)
        self.assertEqual(
            find_failures([entry]), [(SKIA_NAME, CHECK_DYNAMIC_INIT)]
        )


if __name__ == "__main__":
    unittest.main()
