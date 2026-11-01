"""Batch 2 — Seam gate (T4): every isolating seam keeps its injection point.

Covers FR-DEPS-008 (AC-DEPS-021) per specs-cpp-dependencies.md.

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — tools/deps/seam_check.py
does not exist yet (TC-DEPS-021 missing).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

from tools.deps._deps_data import DepsDataError

try:
    from tools.deps import seam_check as sc
except ImportError:
    sc = None

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "seam_check.py")


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


def seam_entry(name, interface, owner="core"):
    """A manifest entry carrying owner/interface/exit_plan."""
    return {
        "name": name,
        "owner": owner,
        "interface": interface,
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
    }


class TestSeamCheck(unittest.TestCase):
    """check_seams: [] clean | violations name the entry | fail closed."""

    def require_module(self):
        self.assertIsNotNone(
            sc, "T4 not implemented: tools.deps.seam_check missing"
        )
        return sc

    def test_clean_manifest_passes(self):
        """Entries with owner/interface/exit_plan on known seams -> [] (happy path)."""
        mod = self.require_module()
        entries = [
            seam_entry("Hermes", "JsRuntime"),
            seam_entry("Cronet", "ITransport", owner="net"),
        ]
        path = write_manifest(entries)
        try:
            self.assertEqual(mod.check_seams(path), [])
        finally:
            os.unlink(path)

    def test_cronet_transport_seam_known(self):
        """ITransport is a known seam: Cronet entry behind it passes (T2 link)."""
        mod = self.require_module()
        path = write_manifest([seam_entry("Cronet", "ITransport", owner="net")])
        try:
            self.assertEqual(mod.check_seams(path), [])
        finally:
            os.unlink(path)

    def test_missing_interface_flagged(self):
        """Blanking an entry's interface fails naming the entry (AC-DEPS-021)."""
        mod = self.require_module()
        path = write_manifest([seam_entry("Hermes", "")])
        try:
            violations = mod.check_seams(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "missing interface must produce a violation")
        self.assertTrue(
            any("Hermes" in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_missing_owner_flagged(self):
        """Entry without an owner fails naming the entry (AC-DEPS-021)."""
        mod = self.require_module()
        entry = seam_entry("Hermes", "JsRuntime")
        entry["owner"] = ""
        path = write_manifest([entry])
        try:
            violations = mod.check_seams(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "missing owner must produce a violation")
        self.assertTrue(
            any("Hermes" in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_missing_exit_plan_flagged(self):
        """Entry without an exit plan fails naming the entry (AC-DEPS-021)."""
        mod = self.require_module()
        entry = seam_entry("Hermes", "JsRuntime")
        entry["exit_plan"] = ""
        path = write_manifest([entry])
        try:
            violations = mod.check_seams(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "missing exit plan must produce a violation")
        self.assertTrue(
            any("Hermes" in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_unknown_seam_flagged(self):
        """Entry naming an unknown seam fails naming the entry (AC-DEPS-021)."""
        mod = self.require_module()
        path = write_manifest([seam_entry("Hermes", "NotARealSeam-xyz")])
        try:
            violations = mod.check_seams(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "unknown seam must produce a violation")
        self.assertTrue(
            any("Hermes" in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_missing_manifest_raises(self):
        """Missing manifest fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        with self.assertRaises(DepsDataError):
            mod.check_seams(os.path.join(REPO_ROOT, "does-not-exist-xyz.json"))

    def test_unparseable_manifest_raises(self):
        """Unparseable manifest fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{ this is not json !!!")
        tmp.close()
        try:
            with self.assertRaises(DepsDataError):
                mod.check_seams(tmp.name)
        finally:
            os.unlink(tmp.name)


class TestSeamCLI(unittest.TestCase):
    """CLI: exit 0 clean/self-test | exit 1 violation/fail-closed | exit 2 usage."""

    def require_script(self):
        self.assertTrue(
            os.path.exists(SCRIPT),
            "T4 not implemented: tools/deps/seam_check.py missing",
        )

    def test_self_test_passes(self):
        """Bundled self-test passes (TC-DEPS-021)."""
        self.require_script()
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_clean_manifest_exit_0(self):
        """Seamed manifest -> exit 0."""
        self.require_script()
        path = write_manifest([seam_entry("Hermes", "JsRuntime")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_blank_interface_exit_1_naming_entry(self):
        """Removed injection point -> exit 1 naming the entry (AC-DEPS-021)."""
        self.require_script()
        path = write_manifest([seam_entry("Hermes", "")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertIn("Hermes", combined)

    def test_missing_manifest_exit_1(self):
        """Missing manifest is exit 1 (fail closed), not success or usage error."""
        self.require_script()
        proc = run_cli(
            "--manifest", os.path.join(REPO_ROOT, "does-not-exist-xyz.json")
        )
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        self.require_script()
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
