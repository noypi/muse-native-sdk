"""Batch 2 — Staging/rollback gate (T5): net -> bundler -> codecs with per-area flags.

Covers FR-DEPS-008 (AC-DEPS-020) per specs-cpp-dependencies.md.

Flags-file shape (ASSUMPTION — the plan's Interface line fixes the function
signature but not the flags file format): *flags_path* points to a JSON
object mapping area name -> rollback flag name, e.g.
``{"net": "rollback-net-xyz", "bundler": "rollback-bundler-xyz",
"codecs": "rollback-codecs-xyz"}``. Exact flag-name strings are the coder's;
the tests assert presence, independence (distinct flags per area), and
staging order only. Manifest entries carry an ``area`` field for the order
check; entries without one are ignored for ordering (ASSUMPTION).

Hermetic: stdlib unittest only, tmp files, no network/secrets/services.
Expected state before implementation: RED — tools/deps/staging_rollback_check.py
does not exist yet (TC-DEPS-020 missing).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

from tools.deps._deps_data import DepsDataError

try:
    from tools.deps import staging_rollback_check as src
except ImportError:
    src = None

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "staging_rollback_check.py")


def run_cli(*args):
    """Run the check CLI hermetically; return CompletedProcess."""
    return subprocess.run(
        [sys.executable, SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )


def write_json(payload):
    """Write a tmp JSON file; return its path."""
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(payload, tmp)
    tmp.close()
    return tmp.name


def staged_entry(name, area):
    """A manifest entry carrying the area used for the staging-order check."""
    return {
        "name": name,
        "owner": "core",
        "interface": "TestInterface",
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
        "area": area,
    }


def full_flags():
    """One independent rollback flag per area (net, bundler, codecs)."""
    return {
        "net": "rollback-net-xyz",
        "bundler": "rollback-bundler-xyz",
        "codecs": "rollback-codecs-xyz",
    }


def ordered_manifest():
    """Manifest entries in net -> bundler -> codecs staging order."""
    return [
        staged_entry("Cronet", "net"),
        staged_entry("Play Core NDK", "bundler"),
        staged_entry("Media3", "codecs"),
    ]


class TestStagingRollbackCheck(unittest.TestCase):
    """check_staging: [] ordered+flags | violations name the area | fail closed."""

    def require_module(self):
        self.assertIsNotNone(
            src, "T5 not implemented: tools.deps.staging_rollback_check missing"
        )
        return src

    def test_ordered_with_flags_passes(self):
        """Staged areas each with an independent flag -> [] (AC-DEPS-020 happy path)."""
        mod = self.require_module()
        manifest = write_json(ordered_manifest())
        flags = write_json(full_flags())
        try:
            self.assertEqual(mod.check_staging(manifest, flags), [])
        finally:
            os.unlink(manifest)
            os.unlink(flags)

    def test_missing_area_flag_flagged(self):
        """Rollout missing an area's rollback flag fails naming the area (AC-DEPS-020)."""
        mod = self.require_module()
        manifest = write_json(ordered_manifest())
        flagset = full_flags()
        del flagset["codecs"]
        flags = write_json(flagset)
        try:
            violations = mod.check_staging(manifest, flags)
        finally:
            os.unlink(manifest)
            os.unlink(flags)
        self.assertTrue(violations, "missing area flag must produce a violation")
        self.assertTrue(
            any("codecs" in v for v in violations),
            "violation must name the area: %r" % (violations,),
        )

    def test_empty_area_flag_flagged(self):
        """Empty rollback flag for an area fails naming the area (AC-DEPS-020)."""
        mod = self.require_module()
        manifest = write_json(ordered_manifest())
        flagset = full_flags()
        flagset["net"] = ""
        flags = write_json(flagset)
        try:
            violations = mod.check_staging(manifest, flags)
        finally:
            os.unlink(manifest)
            os.unlink(flags)
        self.assertTrue(violations, "empty area flag must produce a violation")
        self.assertTrue(
            any("net" in v for v in violations),
            "violation must name the area: %r" % (violations,),
        )

    def test_coupled_flag_flagged(self):
        """One flag shared by two areas fails (flags must be independent)."""
        mod = self.require_module()
        manifest = write_json(ordered_manifest())
        flagset = full_flags()
        flagset["bundler"] = flagset["net"]
        flags = write_json(flagset)
        try:
            violations = mod.check_staging(manifest, flags)
        finally:
            os.unlink(manifest)
            os.unlink(flags)
        self.assertTrue(violations, "coupled flag must produce a violation")
        combined = " ".join(violations)
        self.assertTrue(
            "net" in combined or "bundler" in combined,
            "violation must name the coupled area: %r" % (violations,),
        )

    def test_order_violated_flagged(self):
        """Codecs staged before net fails the staging order (AC-DEPS-020)."""
        mod = self.require_module()
        manifest = write_json(
            [
                staged_entry("Media3", "codecs"),
                staged_entry("Cronet", "net"),
                staged_entry("Play Core NDK", "bundler"),
            ]
        )
        flags = write_json(full_flags())
        try:
            violations = mod.check_staging(manifest, flags)
        finally:
            os.unlink(manifest)
            os.unlink(flags)
        self.assertTrue(violations, "violated staging order must be flagged")

    def test_missing_manifest_raises(self):
        """Missing manifest fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        flags = write_json(full_flags())
        try:
            with self.assertRaises(DepsDataError):
                mod.check_staging(
                    os.path.join(REPO_ROOT, "does-not-exist-xyz.json"), flags
                )
        finally:
            os.unlink(flags)

    def test_unparseable_manifest_raises(self):
        """Unparseable manifest fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{ this is not json !!!")
        tmp.close()
        flags = write_json(full_flags())
        try:
            with self.assertRaises(DepsDataError):
                mod.check_staging(tmp.name, flags)
        finally:
            os.unlink(tmp.name)
            os.unlink(flags)

    def test_missing_flags_raises(self):
        """Missing flags file fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        manifest = write_json(ordered_manifest())
        try:
            with self.assertRaises(DepsDataError):
                mod.check_staging(
                    manifest,
                    os.path.join(REPO_ROOT, "does-not-exist-flags-xyz.json"),
                )
        finally:
            os.unlink(manifest)

    def test_unparseable_flags_raises(self):
        """Unparseable flags file fails closed via DepsDataError (fail-closed)."""
        mod = self.require_module()
        manifest = write_json(ordered_manifest())
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{ this is not json !!!")
        tmp.close()
        try:
            with self.assertRaises(DepsDataError):
                mod.check_staging(manifest, tmp.name)
        finally:
            os.unlink(manifest)
            os.unlink(tmp.name)


class TestStagingRollbackCLI(unittest.TestCase):
    """CLI: exit 0 self-test | exit 2 usage (fail-closed paths covered in unit tests)."""

    def require_script(self):
        self.assertTrue(
            os.path.exists(SCRIPT),
            "T5 not implemented: tools/deps/staging_rollback_check.py missing",
        )

    def test_self_test_passes(self):
        """Bundled self-test passes (TC-DEPS-020)."""
        self.require_script()
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        self.require_script()
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
