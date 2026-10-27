"""Batch 2 — Deny-list enforcement check incl. Boost refusal (T3).

Covers FR-DEPS-003 (AC-DEPS-007) per specs-cpp-dependencies.md:
no engine target includes or links any Boost library, and a build that
references one fails with a message citing this requirement and the
deny-list entry. AC-DEPS-008 needs no code (spec deny-list row already
auditable, verified manually per TC-DEPS-008).

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — ``tools/deps/denylist_check.py``
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
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "denylist_check.py")


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
    entry = {
        "name": name,
        "owner": "core",
        "interface": "TestInterface",
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
    }
    entry.update(overrides)
    return entry


class TestDenylistCheck(unittest.TestCase):
    """T3 CLI: exit 0 clean | exit 1 denied/self-test failure | exit 2 usage."""

    def test_clean_manifest_passes(self):
        """No denied dep linked -> exit 0 (AC-DEPS-007 happy path)."""
        from tools.deps._deps_data import get_allowlist

        names = [e["name"] for e in get_allowlist()][:2]
        path = write_manifest([full_entry(n) for n in names])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_boost_exact_fails_citing_requirement_and_reason(self):
        """A `boost` entry fails citing FR-DEPS-003 and the deny-list reason."""
        from tools.deps._deps_data import get_denylist

        boost = [e for e in get_denylist() if "boost" in e["name"].lower()]
        self.assertTrue(boost, "deny-list must contain a Boost entry")
        reason_snippet = boost[0]["reason"].split()[0].strip("(),;:")
        path = write_manifest([full_entry("boost")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertIn("FR-DEPS-003", combined)
        self.assertIn("boost", combined.lower())
        self.assertTrue(
            reason_snippet.lower() in combined.lower() or "deny" in combined.lower(),
            "failure output must cite the deny-list reason, got: %r" % combined,
        )

    def test_boost_filesystem_variant_fails(self):
        """Substring match: `boost_filesystem` is refused (AC-DEPS-007)."""
        path = write_manifest([full_entry("boost_filesystem")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertIn("FR-DEPS-003", combined)
        self.assertIn("boost_filesystem", combined.lower())

    def test_boost_header_only_subset_fails(self):
        """Header-only subsets are refused with zero exceptions (RULE-DEPS-003)."""
        path = write_manifest([full_entry("boost_headers")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("FR-DEPS-003", proc.stdout + proc.stderr)

    def test_boost_match_is_case_insensitive(self):
        """`Boost` / `BOOST` match the deny-list (case-insensitive)."""
        path = write_manifest([full_entry("Boost"), full_entry("BOOST_REGEX")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("FR-DEPS-003", proc.stdout + proc.stderr)

    def test_folly_denied_dep_fails(self):
        """A non-Boost denied dep (Folly) fails naming the deny-list reason."""
        path = write_manifest([full_entry("Folly")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertIn("folly", combined)
        self.assertTrue(
            "deny" in combined or "reason" in combined,
            "failure output must name the deny-list reason, got: %r" % combined,
        )

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
        """Bundled self-test passes (TC-DEPS-005)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
