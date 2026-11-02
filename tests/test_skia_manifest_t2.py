"""Batch 1 (T2) — Skia Graphite manifest/SBOM row.

Covers FR-DEPS-011 / AC-DEPS-028 per specs-cpp-dependencies.md:
``build/dependency-manifest.json`` names Skia Graphite with owner
``graphics``, isolating interface ``GraphiteRender``, and licence
``BSD-3-Clause``; a missing or mismatched entry fails the manifest check
(TC-DEPS-028).

Hermetic: stdlib unittest only (json reads, tmp manifests for the negative
cases). No downloads, no secrets, no services.
Expected state before implementation: RED — the committed manifest has no
Skia Graphite row, so the row-presence tests fail; only the helper
negative cases pass.
"""

import json
import os
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")

SKIA_NAME = "Skia Graphite"
SKIA_OWNER = "graphics"
SKIA_INTERFACE = "GraphiteRender"
SKIA_LICENCE = "BSD-3-Clause"


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


def row_mismatches(row):
    """Field names whose value differs from the AC-DEPS-028 contract."""
    if row is None:
        return ["missing-entry"]
    bad = []
    if row.get("owner") != SKIA_OWNER:
        bad.append("owner")
    if row.get("interface") != SKIA_INTERFACE:
        bad.append("interface")
    if row.get("licence") != SKIA_LICENCE:
        bad.append("licence")
    return bad


def write_manifest(entries):
    """Write a tmp manifest from entry dicts; return its path."""
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(entries, tmp)
    tmp.close()
    return tmp.name


class TestSkiaManifestRow(unittest.TestCase):
    """AC-DEPS-028: committed manifest carries the Skia Graphite row."""

    def test_manifest_is_valid_json(self):
        """Committed manifest parses to a non-empty entry list."""
        self.assertTrue(load_manifest())

    def test_skia_row_present(self):
        """Skia Graphite row exists (TC-DEPS-028 grep equivalent)."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(
            row, "manifest missing Skia Graphite row (AC-DEPS-028)"
        )

    def test_skia_row_owner(self):
        """Row owner is graphics."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        self.assertEqual(row.get("owner"), SKIA_OWNER)

    def test_skia_row_interface(self):
        """Row isolating interface is GraphiteRender."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        self.assertEqual(row.get("interface"), SKIA_INTERFACE)

    def test_skia_row_licence(self):
        """Row licence is BSD-3-Clause."""
        row = find_skia(load_manifest())
        self.assertIsNotNone(row, "manifest missing Skia Graphite row")
        self.assertEqual(row.get("licence"), SKIA_LICENCE)

    def test_skia_row_fully_matches(self):
        """No mismatched field — a mismatch fails the manifest check."""
        row = find_skia(load_manifest())
        self.assertEqual(row_mismatches(row), [])


class TestSkiaManifestNegatives(unittest.TestCase):
    """AC-DEPS-028: missing/mismatched entries are detectable (fail closed)."""

    def test_missing_entry_flagged(self):
        """A manifest without the row reports a missing entry."""
        path = write_manifest([{"name": "Hermes", "owner": "core"}])
        try:
            self.assertEqual(
                row_mismatches(find_skia(load_manifest(path))),
                ["missing-entry"],
            )
        finally:
            os.unlink(path)

    def test_wrong_owner_flagged(self):
        """owner core instead of graphics is a mismatch (boundary)."""
        path = write_manifest([{
            "name": SKIA_NAME, "owner": "core",
            "interface": SKIA_INTERFACE, "licence": SKIA_LICENCE,
        }])
        try:
            self.assertIn(
                "owner",
                row_mismatches(find_skia(load_manifest(path))),
            )
        finally:
            os.unlink(path)

    def test_wrong_interface_flagged(self):
        """A non-GraphiteRender interface is a mismatch (boundary)."""
        path = write_manifest([{
            "name": SKIA_NAME, "owner": SKIA_OWNER,
            "interface": "SkiaRaster", "licence": SKIA_LICENCE,
        }])
        try:
            self.assertIn(
                "interface",
                row_mismatches(find_skia(load_manifest(path))),
            )
        finally:
            os.unlink(path)

    def test_wrong_licence_flagged(self):
        """A non-BSD-3-Clause licence is a mismatch (boundary)."""
        path = write_manifest([{
            "name": SKIA_NAME, "owner": SKIA_OWNER,
            "interface": SKIA_INTERFACE, "licence": "MIT",
        }])
        try:
            self.assertIn(
                "licence",
                row_mismatches(find_skia(load_manifest(path))),
            )
        finally:
            os.unlink(path)

    def test_case_sensitive_name(self):
        """'skia graphite' (lowercase) does not satisfy the row (boundary)."""
        path = write_manifest([{
            "name": "skia graphite", "owner": SKIA_OWNER,
            "interface": SKIA_INTERFACE, "licence": SKIA_LICENCE,
        }])
        try:
            self.assertIsNone(find_skia(load_manifest(path)))
        finally:
            os.unlink(path)


if __name__ == "__main__":
    unittest.main()
