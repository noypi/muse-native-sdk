"""Batch 3 (T7) — -fno-exceptions render-path proof recorded in manifest.

Covers FR-DEPS-011 / AC-DEPS-034 per specs-cpp-dependencies.md: the
``-fno-exceptions`` render-path proof is taken from the build log and
recorded in the manifest as ``fno_exceptions``; a render-path translation
unit compiled without ``-fno-exceptions`` fails admission (TC-DEPS-034).

Hermetic: stdlib unittest only. Manifest cases read the committed JSON
read-only; gate cases call ``tools.deps.admission_check.find_failures``
in-process on tmp entries; the log-evidence case scans a tmp fixture log
mirroring the GN+NDK compile-line shape. No downloads, no secrets, no
services.
Expected state before implementation: RED — the committed manifest's Skia
Graphite row carries no ``fno_exceptions`` proof (extracted from the build
log only), so the manifest-presence tests fail; only the negative/gate
cases pass.
"""

import json
import os
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")

SKIA_NAME = "Skia Graphite"

sys.path.insert(0, REPO_ROOT)
from tools.deps.admission_check import CHECK_FNO_EXCEPTIONS, find_failures  # noqa: E402


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


def render_path_flagged(log_text, tu):
    """True when the build log shows *tu* compiled with -fno-exceptions."""
    for line in log_text.splitlines():
        if tu in line and "-fno-exceptions" in line:
            return True
    return False


class TestSkiaFnoExceptionsManifest(unittest.TestCase):
    """AC-DEPS-034: proof recorded as manifest fno_exceptions."""

    def test_skia_fno_exceptions_recorded(self):
        """Skia Graphite row records fno_exceptions=True (from build log)."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        self.assertIs(
            row.get("fno_exceptions"), True,
            "fno_exceptions unproven — extract from build log (AC-DEPS-034)",
        )


class TestFnoExceptionsLogEvidence(unittest.TestCase):
    """AC-DEPS-034: the proof shape — render-path TU flagged in build log."""

    def test_flagged_tu_counts_as_proof(self):
        """A compile line with -fno-exceptions is proof for that TU."""
        log = (
            "clang++ -fno-exceptions -c "
            "../../third-party/skia/src/gpu/graphite/Renderer.cpp\n"
        )
        self.assertTrue(render_path_flagged(log, "Renderer.cpp"))

    def test_unflagged_tu_is_no_proof(self):
        """A compile line without the flag is no proof (must fail)."""
        log = (
            "clang++ -fexceptions -c "
            "../../third-party/skia/src/gpu/graphite/Renderer.cpp\n"
        )
        self.assertFalse(render_path_flagged(log, "Renderer.cpp"))

    def test_flag_on_other_tu_is_no_proof(self):
        """The flag must sit on the same TU's line, not a neighbour's."""
        log = (
            "clang++ -fno-exceptions -c ../../third-party/skia/src/core/SkX.cpp\n"
            "clang++ -fexceptions -c "
            "../../third-party/skia/src/gpu/graphite/Renderer.cpp\n"
        )
        self.assertFalse(render_path_flagged(log, "Renderer.cpp"))
        self.assertTrue(render_path_flagged(log, "SkX.cpp"))

    def test_empty_log_is_no_proof(self):
        """An empty build log proves nothing (fail closed, boundary)."""
        self.assertFalse(render_path_flagged("", "Renderer.cpp"))


class TestSkiaFnoExceptionsGate(unittest.TestCase):
    """AC-DEPS-030/034: unproven flag denies under fno-exceptions."""

    def checks_for(self, entry):
        """Failed check names for a single manifest entry."""
        return [check for _, check in find_failures([entry])]

    def test_proven_flag_admits_fno_exceptions(self):
        """fno_exceptions=True passes the fno-exceptions check."""
        self.assertNotIn(
            CHECK_FNO_EXCEPTIONS, self.checks_for(good_entry(SKIA_NAME))
        )

    def test_false_flag_denied(self):
        """fno_exceptions=False denies — render path without flag fails."""
        entry = good_entry(SKIA_NAME, fno_exceptions=False)
        failures = find_failures([entry])
        self.assertIn((SKIA_NAME, CHECK_FNO_EXCEPTIONS), failures)

    def test_missing_flag_denied(self):
        """No fno_exceptions key denies under fno-exceptions."""
        entry = good_entry(SKIA_NAME)
        del entry["fno_exceptions"]
        self.assertIn(CHECK_FNO_EXCEPTIONS, self.checks_for(entry))

    def test_truthy_non_true_denied(self):
        """'true'/1 are not proof — only True admits (boundary)."""
        for wrong in ("true", 1, "yes"):
            with self.subTest(value=wrong):
                self.assertIn(
                    CHECK_FNO_EXCEPTIONS,
                    self.checks_for(good_entry(SKIA_NAME, fno_exceptions=wrong)),
                )

    def test_only_fno_exceptions_fails_in_isolation(self):
        """A False flag fails exactly one check (no cascading)."""
        entry = good_entry(SKIA_NAME, fno_exceptions=False)
        self.assertEqual(
            find_failures([entry]), [(SKIA_NAME, CHECK_FNO_EXCEPTIONS)]
        )


if __name__ == "__main__":
    unittest.main()
