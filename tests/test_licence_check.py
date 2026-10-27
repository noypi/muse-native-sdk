"""Batch 4 — Licence allow-list enforcement check (T6, part 1 of 2).

Covers FR-DEPS-006 (AC-DEPS-013, AC-DEPS-016, AC-DEPS-015) per
specs-cpp-dependencies.md.

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — ``tools/deps/licence_check.py``
is absent so every CLI invocation fails, and
``app/src/main/assets/third-party-notices.txt`` is absent so the
notices-coverage test fails (greenfield for batch 4; batches 1-3 committed
GREEN and are not touched).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "licence_check.py")
DEFAULT_MANIFEST = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")
NOTICES = os.path.join(
    REPO_ROOT, "app", "src", "main", "assets", "third-party-notices.txt"
)


def run_cli(*args):
    """Run the check CLI hermetically; return CompletedProcess."""
    return subprocess.run(
        [sys.executable, SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )


def write_manifest(entries):
    """Write a tmp manifest from a list of entry dicts; return its path."""
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(entries, tmp)
    tmp.close()
    return tmp.name


def full_entry(name, **overrides):
    """A manifest entry carrying every field the manifest schema requires."""
    data = {
        "name": name,
        "owner": "core",
        "interface": "TestInterface",
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
    }
    data.update(overrides)
    return data


class TestLicenceCheck(unittest.TestCase):
    """T6 CLI: exit 0 clean | exit 1 denied/unidentified/self-test fail | exit 2 usage."""

    def test_clean_manifest_passes(self):
        """Allow-listed licences (MIT/Apache-2.0/BSD/ISC/zlib/Unicode/FTL/OFL/CC0) -> exit 0 (AC-DEPS-013 happy path)."""
        path = write_manifest(
            [
                full_entry("lib-a", licence="MIT"),
                full_entry("lib-b", licence="Apache-2.0"),
                full_entry("lib-c", licence="BSD-3-Clause"),
                full_entry("lib-d", licence="ISC"),
                full_entry("lib-e", licence="zlib"),
                full_entry("lib-f", licence="Unicode"),
                full_entry("lib-g", licence="FTL"),
                full_entry("lib-h", licence="OFL"),
                full_entry("lib-i", licence="CC0"),
                full_entry("lib-j", licence="BSD-2-Clause"),
            ]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_gpl_denied(self):
        """GPL licence fails naming the dep (AC-DEPS-013 denied family)."""
        path = write_manifest([full_entry("evil-gpl-lib-xyz", licence="GPL-3.0")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("evil-gpl-lib-xyz", proc.stdout + proc.stderr)

    def test_agpl_denied(self):
        """AGPL licence fails (AC-DEPS-013 denied family)."""
        path = write_manifest([full_entry("evil-agpl-lib-xyz", licence="AGPL-3.0")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("evil-agpl-lib-xyz", proc.stdout + proc.stderr)

    def test_sspl_denied(self):
        """SSPL licence fails (AC-DEPS-013 denied family)."""
        path = write_manifest([full_entry("evil-sspl-lib-xyz", licence="SSPL-1.0")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("evil-sspl-lib-xyz", proc.stdout + proc.stderr)

    def test_noncommercial_denied(self):
        """Non-commercial licence fails (AC-DEPS-013 denied family)."""
        path = write_manifest(
            [full_entry("evil-nc-lib-xyz", licence="CC-BY-NC-4.0 non-commercial")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("evil-nc-lib-xyz", proc.stdout + proc.stderr)

    def test_field_of_use_denied(self):
        """Field-of-use restricted licence fails (AC-DEPS-013 denied family)."""
        path = write_manifest(
            [full_entry("evil-fou-lib-xyz", licence="field-of-use restricted")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("evil-fou-lib-xyz", proc.stdout + proc.stderr)

    def test_missing_licence_denied(self):
        """Missing licence text is treated as denied, never assumed permissive (AC-DEPS-016)."""
        entry = full_entry("nolic-lib-xyz")
        del entry["licence"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("nolic-lib-xyz", proc.stdout + proc.stderr)

    def test_unidentified_licence_denied(self):
        """Unidentifiable licence string is treated as denied (AC-DEPS-016)."""
        path = write_manifest(
            [full_entry("weird-lic-lib-xyz", licence="Totally-Custom-1.0-xyz")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("weird-lic-lib-xyz", proc.stdout + proc.stderr)

    def test_missing_manifest_fails_closed(self):
        """Missing manifest is exit 1 (fail closed), not success or usage error."""
        proc = run_cli(
            "--manifest", os.path.join(REPO_ROOT, "does-not-exist-xyz.json")
        )
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_unparseable_manifest_fails_closed(self):
        """Unparseable manifest is exit 1 (fail closed)."""
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{ this is not json !!!")
        tmp.close()
        try:
            proc = run_cli("--manifest", tmp.name)
        finally:
            os.unlink(tmp.name)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_self_test_passes(self):
        """Bundled self-test passes (TC-DEPS-013)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_self_test_unidentified_passes(self):
        """Bundled unidentified-licence self-test passes (TC-DEPS-016)."""
        proc = run_cli("--self-test-unidentified")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


class TestNoticesFile(unittest.TestCase):
    """TC-DEPS-015: notices file lists every manifest dep with licence text."""

    def test_notices_covers_every_manifest_dep(self):
        """Every dep in the committed manifest appears in the notices file with licence text (AC-DEPS-015)."""
        with open(DEFAULT_MANIFEST, "r", encoding="utf-8") as handle:
            data = json.load(handle)
        entries = data["dependencies"] if isinstance(data, dict) else data
        with open(NOTICES, "r", encoding="utf-8") as handle:
            notices = handle.read()
        self.assertTrue(entries, "committed manifest must not be empty")
        for entry in entries:
            name = entry["name"]
            licence = entry.get("licence", "")
            self.assertIn(
                name,
                notices,
                "notices file missing dependency %r (AC-DEPS-015)" % name,
            )
            self.assertTrue(
                licence and licence in notices,
                "notices file missing licence text %r for %r (AC-DEPS-015)"
                % (licence, name),
            )


if __name__ == "__main__":
    unittest.main()
