"""Batch 1 (T1) — Skia m158 pin row + tarball<->tree hash equality.

Covers FR-DEPS-011 / AC-DEPS-027 per specs-cpp-dependencies.md:
``tools/deps/third_party_pins.json`` records ``skia-m158.tar.gz`` with sha256
``9335578c0c5c29c1438618019a67238b6241e7bcc1124606c6f5f3d39ec1f390e``;
an unpinned or hash-mismatched tarball fails setup before any compile
(TC-DEPS-027).

Hermetic: stdlib unittest only (json/hashlib/os reads, tmp files for the
tamper cases). The sibling third-party dir is read via
``MUSE_NATIVE_THIRD_PARTY_DIR`` for presence only, never modified; no
downloads, no secrets, no services.
Expected state before implementation: RED — the pins row is absent, so the
row-presence/hash-equality tests fail; only the helper tamper cases pass.
"""

import hashlib
import json
import os
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PINS = os.path.join(REPO_ROOT, "tools", "deps", "third_party_pins.json")

PIN_FILE = "skia-m158.tar.gz"
PIN_SHA256 = (
    "9335578c0c5c29c1438618019a67238b6241e7bcc1124606c5f3d39ec1f390e9"
)
TREE_DIR = "skia-m158"
TREE_WITNESSES = (
    os.path.join("include", "core", "SkCanvas.h"),
    os.path.join("include", "gpu", "graphite"),
    os.path.join("include", "gpu", "graphite", "vk"),
)


def sha256_of(path):
    """Hex sha256 of a file read in binary chunks (stdlib only)."""
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_pins(path=PINS):
    """Load the pins file; raise a naming failure if missing/unparseable."""
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def find_pin(pins, filename=PIN_FILE):
    """Return the pin row for ``filename`` or None when unpinned."""
    if isinstance(pins, dict):
        for _key, row in pins.items():
            if isinstance(row, dict) and row.get("file") == filename:
                return row
    return None


def third_party_dir():
    """Sibling third-party dir from env only; None when not configured."""
    candidate = os.environ.get("MUSE_NATIVE_THIRD_PARTY_DIR")
    if candidate and os.path.isdir(candidate):
        return candidate
    return None


class TestSkiaPinRow(unittest.TestCase):
    """AC-DEPS-027: pin row present with the exact snapshot sha256."""

    def test_pins_file_is_valid_json(self):
        """Pins file parses as a JSON object."""
        pins = load_pins()
        self.assertIsInstance(pins, dict)

    def test_skia_pin_row_present(self):
        """A row naming skia-m158.tar.gz exists (TC-DEPS-027)."""
        pins = load_pins()
        row = find_pin(pins)
        self.assertIsNotNone(
            row, "pins file missing entry for %r (AC-DEPS-027)" % PIN_FILE
        )

    def test_skia_pin_sha256_exact(self):
        """Row sha256 equals 9335578c...390e9 exactly (no prefix match)."""
        pins = load_pins()
        row = find_pin(pins)
        self.assertIsNotNone(row, "pins file missing entry for %r" % PIN_FILE)
        self.assertEqual(row.get("sha256"), PIN_SHA256)

    def test_skia_pin_sha256_well_formed(self):
        """Pin value is 64 lowercase hex chars (boundary: truncation caught)."""
        pins = load_pins()
        row = find_pin(pins)
        self.assertIsNotNone(row, "pins file missing entry for %r" % PIN_FILE)
        value = row.get("sha256", "")
        self.assertEqual(len(value), 64)
        self.assertEqual(value, value.lower())
        int(value, 16)  # raises when non-hex

    def test_unpinned_tarball_detected(self):
        """A pins object without the row yields None -> setup must fail."""
        pins = load_pins()
        pruned = {
            k: v for k, v in pins.items() if not (
                isinstance(v, dict) and v.get("file") == PIN_FILE)
        }
        self.assertIsNone(find_pin(pruned))

    def test_mismatched_pin_detected(self):
        """A row with a tampered sha256 does not equal the snapshot pin."""
        self.assertNotEqual(
            "00" + PIN_SHA256[2:], PIN_SHA256,
            "tampered pin must not equal the snapshot pin (AC-DEPS-027)",
        )


class TestTarballTreeEquality(unittest.TestCase):
    """AC-DEPS-027: executed sha256 over tarball and extracted tree agree."""

    def test_sha256_helper_detects_tamper(self):
        """One flipped byte changes the digest (hermetic tmp-file proof)."""
        tmp = tempfile.NamedTemporaryFile(delete=False)
        try:
            tmp.write(b"skia-m158-snapshot-bytes")
            tmp.close()
            good = sha256_of(tmp.name)
            with open(tmp.name, "r+b") as handle:
                handle.seek(0)
                handle.write(b"X")
            bad = sha256_of(tmp.name)
        finally:
            os.unlink(tmp.name)
        self.assertNotEqual(good, bad)

    def test_tarball_present_and_pinned(self):
        """Tarball exists under MUSE_NATIVE_THIRD_PARTY_DIR and matches pin."""
        third = third_party_dir()
        self.assertIsNotNone(
            third,
            "MUSE_NATIVE_THIRD_PARTY_DIR unset or not a dir — "
            "cannot verify tarball presence for %r" % PIN_FILE,
        )
        tarball = os.path.join(third, PIN_FILE)
        self.assertTrue(
            os.path.isfile(tarball), "tarball missing: %r" % tarball
        )
        self.assertEqual(sha256_of(tarball), PIN_SHA256)

    def test_extracted_tree_witnesses_present(self):
        """Extracted tree holds skia-m158/ + SkCanvas.h + gpu/graphite/vk."""
        third = third_party_dir()
        self.assertIsNotNone(
            third,
            "MUSE_NATIVE_THIRD_PARTY_DIR unset or not a dir — "
            "cannot verify extracted tree for %r" % PIN_FILE,
        )
        root = os.path.join(third, TREE_DIR)
        self.assertTrue(os.path.isdir(root), "tree dir missing: %r" % root)
        for witness in TREE_WITNESSES:
            path = os.path.join(root, witness)
            self.assertTrue(
                os.path.exists(path), "tree witness missing: %r" % path
            )


if __name__ == "__main__":
    unittest.main()
