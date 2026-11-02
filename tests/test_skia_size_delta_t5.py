"""Batch 3 (T5) — Executed measured size delta via size_delta.sh.

Covers FR-DEPS-011 / AC-DEPS-032 per specs-cpp-dependencies.md:
reference-vs-candidate binaries built from the same commit via
``tools/deps/size_delta.sh``, and the measured value recorded in
``build/dependency-manifest.json`` as ``size_delta_bytes`` charged against
the APK budget; a missing or unmeasured value fails admission (TC-DEPS-032).

Hermetic: stdlib unittest only. CLI cases drive the committed shell script
with tmp fixtures via subprocess; manifest cases read the committed JSON
read-only and use tmp dicts for negatives; gate cases call
``tools.deps.admission_check.find_failures`` in-process on tmp entries.
No downloads, no secrets, no services.
Expected state before implementation: RED — the committed manifest's Skia
Graphite row carries no ``size_delta_bytes`` (Batch 2/3 measured fields are
filled by lane execution only), so the manifest-presence tests fail; only
the CLI-format and negative/gate cases pass.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "size_delta.sh")
MANIFEST = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")

SKIA_NAME = "Skia Graphite"

sys.path.insert(0, REPO_ROOT)
from tools.deps.admission_check import CHECK_SIZE_DELTA, find_failures  # noqa: E402


def run_cli(*args):
    """Run size_delta.sh hermetically; return CompletedProcess."""
    return subprocess.run(
        [SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )


def make_fixture(path, size):
    """Write a fixed-size zero-filled fixture file."""
    with open(path, "wb") as handle:
        handle.write(b"\0" * size)


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


class TestSizeDeltaCli(unittest.TestCase):
    """AC-DEPS-032: size_delta.sh measures (never estimates) the delta."""

    def test_growth_delta_format(self):
        """100 -> 150 reports size_delta_bytes=50 plus the PR line."""
        with tempfile.TemporaryDirectory() as tmp:
            ref = os.path.join(tmp, "ref.bin")
            cand = os.path.join(tmp, "cand.bin")
            make_fixture(ref, 100)
            make_fixture(cand, 150)
            proc = run_cli("--reference", ref, "--candidate", cand)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("size_delta_bytes=50", proc.stdout)
        self.assertIn(
            "Dependency size delta (same-commit reference): 50 bytes (100 → 150)",
            proc.stdout,
        )

    def test_shrinkage_negative_delta(self):
        """200 -> 150 reports a signed negative delta (boundary)."""
        with tempfile.TemporaryDirectory() as tmp:
            ref = os.path.join(tmp, "ref.bin")
            cand = os.path.join(tmp, "cand.bin")
            make_fixture(ref, 200)
            make_fixture(cand, 150)
            proc = run_cli("--reference", ref, "--candidate", cand)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("size_delta_bytes=-50", proc.stdout)

    def test_identical_zero_delta(self):
        """Identical binaries report a zero delta (boundary)."""
        with tempfile.TemporaryDirectory() as tmp:
            ref = os.path.join(tmp, "ref.bin")
            cand = os.path.join(tmp, "cand.bin")
            make_fixture(ref, 128)
            make_fixture(cand, 128)
            proc = run_cli("--reference", ref, "--candidate", cand)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertIn("size_delta_bytes=0", proc.stdout)

    def test_missing_candidate_fails_closed(self):
        """Missing candidate emits no delta line and exits 1."""
        with tempfile.TemporaryDirectory() as tmp:
            ref = os.path.join(tmp, "ref.bin")
            make_fixture(ref, 64)
            proc = run_cli(
                "--reference", ref,
                "--candidate", os.path.join(tmp, "nope.bin"),
            )
        self.assertEqual(proc.returncode, 1)
        self.assertNotIn("size_delta_bytes=", proc.stdout)

    def test_missing_reference_fails_closed(self):
        """Missing reference emits no delta line and exits 1."""
        with tempfile.TemporaryDirectory() as tmp:
            cand = os.path.join(tmp, "cand.bin")
            make_fixture(cand, 64)
            proc = run_cli(
                "--reference", os.path.join(tmp, "nope.bin"),
                "--candidate", cand,
            )
        self.assertEqual(proc.returncode, 1)
        self.assertNotIn("size_delta_bytes=", proc.stdout)

    def test_incomplete_args_fail_with_usage(self):
        """No binaries at all fails closed with a usage message."""
        proc = run_cli()
        self.assertNotEqual(proc.returncode, 0)
        combined = proc.stdout + proc.stderr
        self.assertIn("usage:", combined)


class TestSkiaSizeDeltaManifest(unittest.TestCase):
    """AC-DEPS-032: measured value recorded as manifest size_delta_bytes."""

    def test_skia_size_delta_recorded(self):
        """Skia Graphite row carries an int size_delta_bytes (measured)."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        value = row.get("size_delta_bytes")
        self.assertIsInstance(value, int, "size_delta_bytes unmeasured")
        self.assertNotIsInstance(value, bool, "bool is not a measurement")

    def test_skia_size_delta_not_string(self):
        """A string value (estimated/hand-written) is not a measurement."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        self.assertNotIsInstance(row.get("size_delta_bytes"), str)


class TestSkiaSizeDeltaGate(unittest.TestCase):
    """AC-DEPS-030/032: missing/unmeasured value denies under size-delta."""

    def checks_for(self, entry):
        """Failed check names for a single manifest entry."""
        return [check for _, check in find_failures([entry])]

    def test_measured_value_admits_size_delta(self):
        """An int size_delta_bytes passes the size-delta check."""
        self.assertNotIn(
            CHECK_SIZE_DELTA, self.checks_for(good_entry(SKIA_NAME))
        )

    def test_missing_value_denied(self):
        """No size_delta_bytes denies under size-delta (fail closed)."""
        entry = good_entry(SKIA_NAME)
        del entry["size_delta_bytes"]
        self.assertIn(CHECK_SIZE_DELTA, self.checks_for(entry))

    def test_none_value_denied(self):
        """Explicit null denies under size-delta (boundary)."""
        self.assertIn(
            CHECK_SIZE_DELTA,
            self.checks_for(good_entry(SKIA_NAME, size_delta_bytes=None)),
        )

    def test_string_value_denied(self):
        """A hand-written string estimate denies under size-delta."""
        self.assertIn(
            CHECK_SIZE_DELTA,
            self.checks_for(good_entry(SKIA_NAME, size_delta_bytes="120")),
        )

    def test_bool_value_denied(self):
        """A bool is wrong-typed evidence and denies under size-delta."""
        self.assertIn(
            CHECK_SIZE_DELTA,
            self.checks_for(good_entry(SKIA_NAME, size_delta_bytes=True)),
        )

    def test_only_size_delta_fails_in_isolation(self):
        """A missing value fails exactly one check (no cascading)."""
        entry = good_entry(SKIA_NAME)
        del entry["size_delta_bytes"]
        self.assertEqual(find_failures([entry]), [(SKIA_NAME, CHECK_SIZE_DELTA)])


if __name__ == "__main__":
    unittest.main()
