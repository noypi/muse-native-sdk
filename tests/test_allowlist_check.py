"""Batch 1 — Allow-list enforcement check (T2) + shared deps-data module (T1, indirect).

Covers FR-DEPS-002 (AC-DEPS-004/006), FR-DEPS-003 (AC-DEPS-007),
FR-DEPS-004 (AC-DEPS-009) per specs-cpp-dependencies.md.

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — imports of
``tools.deps._deps_data`` / ``tools/deps/allowlist_check.py`` fail because
nothing is implemented yet (greenfield checkout).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "allowlist_check.py")


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


class TestDepsDataModule(unittest.TestCase):
    """T1 shared module, exercised indirectly (no CLI; imported by T2-T7)."""

    def test_allowlist_contains_spec_entries(self):
        """Allow-list names every approved library (AC-DEPS-004)."""
        from tools.deps._deps_data import get_allowlist

        names = [e["name"] for e in get_allowlist()]
        self.assertGreaterEqual(len(names), 2)
        self.assertTrue(
            any("ankerl" in n and "unordered_dense" in n for n in names),
            "allow-list must contain ankerl::unordered_dense",
        )
        self.assertTrue(
            any("flatbuffers" in n.lower() or "FlatBuffers" in n for n in names),
            "allow-list must contain FlatBuffers",
        )
        for entry in get_allowlist():
            for field in ("name", "owner", "need"):
                self.assertTrue(
                    entry.get(field),
                    "allow-list entry missing non-empty %r: %r" % (field, entry),
                )

    def test_denylist_boost_cites_four_reasons(self):
        """Boost deny entry states all four auditable reasons (AC-DEPS-008)."""
        from tools.deps._deps_data import get_denylist

        boost = [e for e in get_denylist() if "boost" in e["name"].lower()]
        self.assertTrue(boost, "deny-list must contain a Boost entry")
        reason = boost[0]["reason"].lower()
        self.assertIn("core-012", reason)  # 1. dynamic initializers
        self.assertIn("core-001", reason)  # 2. exception assumptions
        self.assertTrue(
            "footprint" in reason or "compile" in reason or "apk" in reason,
            "Boost reason must cite footprint/compile-time cost",
        )
        self.assertTrue(
            "c++20" in reason or "pmr" in reason,
            "Boost reason must cite C++20/std::pmr redundancy",
        )

    def test_load_manifest_missing_raises(self):
        """Missing manifest fails closed (plan A1)."""
        from tools.deps._deps_data import DepsDataError, load_manifest

        with self.assertRaises(DepsDataError):
            load_manifest(os.path.join(REPO_ROOT, "does-not-exist-xyz.json"))

    def test_load_manifest_unparseable_raises(self):
        """Unparseable manifest fails closed (plan A1)."""
        from tools.deps._deps_data import DepsDataError, load_manifest

        path = write_manifest("not-a-list")
        try:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("{ this is not json !!!")
            with self.assertRaises(DepsDataError):
                load_manifest(path)
        finally:
            os.unlink(path)


class TestAllowlistCheck(unittest.TestCase):
    """T2 CLI: exit 0 clean | exit 1 unknown/self-test failure | exit 2 usage."""

    def test_clean_manifest_passes(self):
        """All linked deps allow-listed -> exit 0 (AC-DEPS-004 happy path)."""
        from tools.deps._deps_data import get_allowlist

        names = [e["name"] for e in get_allowlist()][:2]
        path = write_manifest([full_entry(n) for n in names])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_unknown_dep_fails_closed(self):
        """Unlisted dep fails naming it and the missing entry (AC-DEPS-004/006)."""
        path = write_manifest([full_entry("evil-lib-not-allowlisted-xyz")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("evil-lib-not-allowlisted-xyz", combined)
        self.assertIn("allow-list", combined)

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
        """Bundled self-test passes (TC-DEPS-004)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
