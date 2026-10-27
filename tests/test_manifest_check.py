"""Batch 2 — Ownership/manifest check: owner + interface + exit plan (T4).

Covers FR-DEPS-004 (AC-DEPS-009, AC-DEPS-010) per
specs-cpp-dependencies.md: every admitted dependency names an owning
subsystem and an isolating interface plus an exit plan; a dependency with
no owner, no isolating interface or no exit plan fails the
dependency-manifest check (TC-DEPS-010; TC-DEPS-009 is the manual
acceptance on the T1 manifest fixture).

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — ``tools/deps/manifest_check.py``
does not exist, so every CLI test fails (interpreter exit 2 / missing file)
and only the usage-error test can coincidentally pass.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "manifest_check.py")


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


def full_entry(name):
    """A manifest entry carrying every field the manifest schema requires."""
    return {
        "name": name,
        "owner": "core",
        "interface": "TestInterface",
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
    }


class TestManifestCheck(unittest.TestCase):
    """T4 CLI: exit 0 complete | exit 1 gap/self-test failure | exit 2 usage."""

    def test_full_manifest_passes(self):
        """Every entry has owner + interface + exit_plan -> exit 0 (AC-DEPS-009)."""
        from tools.deps._deps_data import get_allowlist

        names = [e["name"] for e in get_allowlist()][:2]
        path = write_manifest([full_entry(n) for n in names])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_real_manifest_passes(self):
        """The committed manifest satisfies the ownership check (TC-DEPS-009)."""
        manifest = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")
        proc = run_cli("--manifest", manifest)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_missing_owner_fails(self):
        """An entry with no owner fails (AC-DEPS-010)."""
        entry = full_entry("Hermes")
        del entry["owner"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("hermes", combined)
        self.assertIn("owner", combined)

    def test_empty_owner_fails(self):
        """An entry with an empty owner fails (AC-DEPS-010)."""
        entry = full_entry("Hermes")
        entry["owner"] = ""
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("hermes", combined)
        self.assertIn("owner", combined)

    def test_missing_interface_fails(self):
        """An entry with no isolating interface fails (AC-DEPS-009/010)."""
        entry = full_entry("Yoga")
        del entry["interface"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("yoga", combined)
        self.assertIn("interface", combined)

    def test_missing_exit_plan_fails(self):
        """An entry with no exit plan fails (AC-DEPS-010)."""
        entry = full_entry("FlatBuffers")
        del entry["exit_plan"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("flatbuffers", combined)
        self.assertIn("exit_plan", combined)

    def test_multiple_gaps_all_named(self):
        """Stdout names each offending dep and its missing field(s)."""
        bad_owner = full_entry("Oboe")
        bad_owner["owner"] = "  "
        bad_both = full_entry("WebRTC")
        del bad_both["interface"]
        del bad_both["exit_plan"]
        path = write_manifest([full_entry("Hermes"), bad_owner, bad_both])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("oboe", combined)
        self.assertIn("webrtc", combined)
        self.assertIn("owner", combined)
        self.assertIn("interface", combined)
        self.assertIn("exit_plan", combined)

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
        """Bundled self-test passes (TC-DEPS-010)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
