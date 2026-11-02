#!/usr/bin/env python3
"""Batch 5 (T8) hermetic unit tests for the production-link gate.

Covers FR-DEPS-008 / AC-DEPS-019 (no mock/test-double reachable from the
production link set; every linked lib allow-listed) plus the fail-closed
manifest contract shared with AC-DEPS-021's seam gate. AC-DEPS-020
(staging/rollback flags) and AC-DEPS-021 (seam injection points) are owned
by `staging_rollback_check.py` / `seam_check.py`, not asserted here.

Hermetic: stdlib `unittest` only. Every fixture manifest is built inline
and written to a `tempfile` owned by the test process; no network, no
`.env`, no application configuration, no live service, no subprocess
except the one LIVE case marked below (reads only the committed tree
manifest, asserts the shipped gate exit code).

The gate's own `--self-test` already pins the same six behaviors; these
cases re-assert them through the public boundary (`check_production_links`
+ the `production_link_check.py --manifest` CLI) so the batch has an
independent red/green signal owned by the tester.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.deps._deps_data import DepsDataError, get_allowlist
from tools.deps.production_link_check import check_production_links


def _entry(name, **overrides):
    record = {
        "name": name,
        "owner": "core",
        "interface": "TestInterface",
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
    }
    record.update(overrides)
    return record


def _write_manifest(payload):
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(payload, tmp)
    tmp.close()
    return tmp.name


class ProductionLinkGateTest(unittest.TestCase):
    def _check_entries(self, entries):
        path = _write_manifest(entries)
        try:
            return check_production_links(path)
        finally:
            os.unlink(path)

    def test_allowlisted_entries_pass(self):
        names = [item["name"] for item in get_allowlist()][:3]
        self.assertEqual(
            self._check_entries([_entry(n) for n in names]), []
        )

    def test_object_shaped_manifest_passes(self):
        names = [item["name"] for item in get_allowlist()][:2]
        path = _write_manifest(
            {"dependencies": [_entry(n) for n in names]}
        )
        try:
            self.assertEqual(check_production_links(path), [])
        finally:
            os.unlink(path)

    def test_unknown_dep_fails_naming_it(self):
        bad = "evil-lib-not-allowlisted-xyz"
        violations = self._check_entries([_entry(bad)])
        self.assertTrue(violations)
        self.assertTrue(any(bad in v for v in violations))

    def test_mock_named_entry_fails_naming_it(self):
        bad = "MockHermes"
        violations = self._check_entries([_entry(bad)])
        self.assertTrue(violations)
        self.assertTrue(any(bad in v for v in violations))

    def test_fake_named_entry_fails_naming_it(self):
        bad = "fake-hermes-for-tests"
        violations = self._check_entries([_entry(bad)])
        self.assertTrue(violations)
        self.assertTrue(any(bad in v for v in violations))

    def test_stub_named_entry_fails_naming_it(self):
        bad = "stub-codec-module"
        violations = self._check_entries([_entry(bad)])
        self.assertTrue(violations)
        self.assertTrue(any(bad in v for v in violations))

    def test_double_named_entry_fails_naming_it(self):
        bad = "TestDoubleAudio"
        violations = self._check_entries([_entry(bad)])
        self.assertTrue(violations)
        self.assertTrue(any(bad in v for v in violations))

    def test_double_match_is_case_insensitive(self):
        bad = "MOCK-text-shaper"
        violations = self._check_entries([_entry(bad)])
        self.assertTrue(violations)
        self.assertTrue(any(bad in v for v in violations))

    def test_unnamed_entry_fails_closed(self):
        violations = self._check_entries([{"owner": "core"}])
        self.assertTrue(violations)

    def test_missing_manifest_raises_fail_closed(self):
        missing = os.path.join(
            tempfile.gettempdir(), "t8-does-not-exist-xyz.json"
        )
        if os.path.exists(missing):
            os.unlink(missing)
        with self.assertRaises(DepsDataError):
            check_production_links(missing)

    def test_unparseable_manifest_raises_fail_closed(self):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{ this is not json !!!")
        tmp.close()
        try:
            with self.assertRaises(DepsDataError):
                check_production_links(tmp.name)
        finally:
            os.unlink(tmp.name)

    def test_non_list_manifest_raises_fail_closed(self):
        path = _write_manifest({"no_dependencies_key": []})
        try:
            with self.assertRaises(DepsDataError):
                check_production_links(path)
        finally:
            os.unlink(path)

    def test_live_tree_manifest_passes_gate(self):
        """LIVE (not hermetic): the shipped gate passes on this tree."""
        manifest = os.path.join(_REPO_ROOT, "build", "dependency-manifest.json")
        gate = os.path.join(
            _REPO_ROOT, "tools", "deps", "production_link_check.py"
        )
        proc = subprocess.run(
            [sys.executable, gate, "--manifest", manifest],
            capture_output=True,
            text=True,
            timeout=120,
        )
        self.assertEqual(
            proc.returncode,
            0,
            "gate failed on the live tree manifest:\n%s\n%s"
            % (proc.stdout, proc.stderr),
        )


if __name__ == "__main__":
    unittest.main()
