"""Batch 2 — Production-link gate (T3): no hermetic double reachable from production.

Covers FR-DEPS-008 (AC-DEPS-019) and FR-TESTING-005 (AC-TESTING-036,
production side) per specs-cpp-dependencies.md and specs-testing.md.

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — tools/deps/production_link_check.py
does not exist yet (TC-DEPS-019 missing).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

from tools.deps._deps_data import DepsDataError

try:
    from tools.deps import production_link_check as plc
except ImportError:
    plc = None

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "production_link_check.py")


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


class TestProductionLinkCheck(unittest.TestCase):
    """check_production_links: [] clean | violations name the entry | fail closed."""

    def require_module(self):
        self.assertIsNotNone(
            plc, "T3 not implemented: tools.deps.production_link_check missing"
        )
        return plc

    def test_clean_manifest_passes(self):
        """Allow-listed entries link clean -> [] (AC-DEPS-019 happy path)."""
        mod = self.require_module()
        from tools.deps._deps_data import get_allowlist

        names = [e["name"] for e in get_allowlist()][:2]
        path = write_manifest([full_entry(n) for n in names])
        try:
            self.assertEqual(mod.check_production_links(path), [])
        finally:
            os.unlink(path)

    def test_unknown_dep_flagged(self):
        """Non-allow-list entry fails naming it (AC-DEPS-019)."""
        mod = self.require_module()
        bad = "evil-lib-not-allowlisted-xyz"
        path = write_manifest([full_entry(bad)])
        try:
            violations = mod.check_production_links(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "unknown dep must produce a violation")
        self.assertTrue(
            any(bad in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_mock_named_entry_flagged(self):
        """Manifest entry named as a mock double fails naming it (AC-DEPS-019)."""
        mod = self.require_module()
        bad = "MockHermes"
        path = write_manifest([full_entry(bad)])
        try:
            violations = mod.check_production_links(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "mock-named entry must produce a violation")
        self.assertTrue(
            any(bad in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_double_symbol_entry_flagged(self):
        """Test-double-symbol entry fails naming it (AC-DEPS-019)."""
        mod = self.require_module()
        bad = "fake-hermes-for-tests"
        path = write_manifest([full_entry(bad)])
        try:
            violations = mod.check_production_links(path)
        finally:
            os.unlink(path)
        self.assertTrue(violations, "double-symbol entry must produce a violation")
        self.assertTrue(
            any(bad in v for v in violations),
            "violation must name the entry: %r" % (violations,),
        )

    def test_empty_manifest_passes(self):
        """Empty manifest links nothing -> [] (boundary; ASSUMPTION)."""
        mod = self.require_module()
        path = write_manifest([])
        try:
            self.assertEqual(mod.check_production_links(path), [])
        finally:
            os.unlink(path)

    def test_missing_manifest_raises(self):
        """Missing manifest fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        with self.assertRaises(DepsDataError):
            mod.check_production_links(
                os.path.join(REPO_ROOT, "does-not-exist-xyz.json")
            )

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
                mod.check_production_links(tmp.name)
        finally:
            os.unlink(tmp.name)


class TestProductionLinkCLI(unittest.TestCase):
    """CLI: exit 0 clean/self-test | exit 1 violation/fail-closed | exit 2 usage."""

    def require_script(self):
        self.assertTrue(
            os.path.exists(SCRIPT),
            "T3 not implemented: tools/deps/production_link_check.py missing",
        )

    def test_self_test_passes(self):
        """Bundled self-test passes (TC-DEPS-019)."""
        self.require_script()
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_clean_manifest_exit_0(self):
        """Allow-listed manifest -> exit 0."""
        self.require_script()
        from tools.deps._deps_data import get_allowlist

        names = [e["name"] for e in get_allowlist()][:2]
        path = write_manifest([full_entry(n) for n in names])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_mock_entry_exit_1_naming_entry(self):
        """Mock-double entry -> exit 1 naming the entry (AC-DEPS-019)."""
        self.require_script()
        bad = "MockHermes"
        path = write_manifest([full_entry(bad)])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertIn(bad, combined)

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
