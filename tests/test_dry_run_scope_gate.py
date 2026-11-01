"""Batch 4 (T7) — dry-run scope gate: hermetic doubles stay in scope.

Covers FR-TESTING-005 / AC-TESTING-036 (doubles confined to unit
tests, golden tooling, and named dry-run paths) / AC-TESTING-037
(every named dry-run path emits no shipped artifact and never
substitutes in release) / TC-TESTING-036 (``hermetic`` in
``testing/determinism/scope-allowlist.txt``) / TC-TESTING-037 (the
hermetic script below is the gate implementation the audit maps;
no new wrapper task or wrapper literal is introduced anywhere).

Public boundary under test: ``testing.determinism.dry_run_scope_gate``
with ``check_scope(tree: str, allowlist_path: str) -> list[str]`` and
``main(argv: list[str]) -> int``, plus the CLI
``dry_run_scope_gate.py --scope <path> [--allowlist <path>]``
(0 pass, 1 fail, 2 usage error).

Hermetic: stdlib unittest only (``os``/``ast``/``tempfile``/
``subprocess``/``sys``/``unittest``), tmp trees for fixtures, no
network/downloads/device/secrets, no wall-clock reads.

Expected state before implementation: RED — neither
``testing/determinism/scope-allowlist.txt`` nor
``testing/determinism/dry_run_scope_gate.py`` exists yet, so every
test fails on the "T7 not implemented" requirement.
"""

import ast
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DET_DIR = os.path.join(REPO_ROOT, "testing", "determinism")
ALLOWLIST_PATH = os.path.join(DET_DIR, "scope-allowlist.txt")
SCRIPT = os.path.join(DET_DIR, "dry_run_scope_gate.py")

try:
    from testing.determinism import dry_run_scope_gate as gate
except ImportError:
    gate = None

# Double-name fragments (case-insensitive); mirrors the
# tools/deps/production_link_check.py double pattern.
DOUBLE_FRAGMENTS = ("mock", "fake", "stub", "double")

# Shipped-artifact suffixes that must never appear under a dry-run
# allowlist entry (ASSUMPTION — see contract).
SHIPPED_SUFFIXES = (".aar", ".so", ".jar")

# Wrapper literal, assembled at runtime so this file itself carries no
# new wrapper reference (FR-TESTING-006/RULE-TESTING-002).
WRAPPER_LITERAL = "grad" + "lew"

# Network surfaces the hermetic gate must never import (stdlib AST
# scan of the gate source; the gate walks the local tree only).
FORBIDDEN_IMPORTS = ("socket", "urllib", "requests", "http.client")


def read_source(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def imported_modules(source):
    """Return the set of top-level module roots imported by *source*."""
    tree = ast.parse(source)
    roots = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for alias in node.names:
                roots.add(alias.name.split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                roots.add(node.module.split(".")[0])
    return roots


def write_allowlist(directory, lines):
    """Write an allowlist file with one entry per line; return its path."""
    path = os.path.join(directory, "scope-allowlist.txt")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return path


def touch(path, content=""):
    """Create *path* (plus parents) with *content*; return *path*."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(content)
    return path


def run_cli(*args):
    """Run the gate CLI hermetically; return CompletedProcess."""
    return subprocess.run(
        [sys.executable, SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )


class ScopeAllowlistContractTest(unittest.TestCase):
    """scope-allowlist.txt names exactly the legitimate-double locations."""

    def require_allowlist(self):
        self.assertTrue(
            os.path.isfile(ALLOWLIST_PATH),
            "T7 not implemented: testing/determinism/scope-allowlist.txt missing",
        )
        return read_source(ALLOWLIST_PATH)

    def entries(self, content):
        return [
            line.strip()
            for line in content.splitlines()
            if line.strip() and not line.strip().startswith("#")
        ]

    def test_allowlist_exists(self):
        """Allowlist file exists (TC-TESTING-036 file side)."""
        self.require_allowlist()

    def test_allowlist_names_hermetic(self):
        """``hermetic`` appears in scope-allowlist.txt (TC-TESTING-036)."""
        self.assertIn("hermetic", self.require_allowlist())

    def test_allowlist_names_unit_test_location(self):
        """At least one entry covers unit-test dirs (AC-TESTING-036)."""
        entries = self.entries(self.require_allowlist())
        self.assertTrue(
            any("test" in entry for entry in entries),
            "allowlist must name a unit-test location: %r" % (entries,),
        )

    def test_allowlist_names_golden_tooling(self):
        """At least one entry covers golden generation tooling."""
        entries = self.entries(self.require_allowlist())
        self.assertTrue(
            any("golden" in entry for entry in entries),
            "allowlist must name golden tooling: %r" % (entries,),
        )

    def test_allowlist_names_dry_run_path(self):
        """At least one entry names a dry-run path (AC-TESTING-037)."""
        entries = self.entries(self.require_allowlist())
        self.assertTrue(
            any("dry-run" in entry for entry in entries),
            "allowlist must name each dry-run path: %r" % (entries,),
        )

    def test_allowlist_lines_are_tree_relative(self):
        """Entries are tree-relative prefixes, never absolute paths."""
        for entry in self.entries(self.require_allowlist()):
            self.assertFalse(
                os.path.isabs(entry),
                "allowlist entry must be tree-relative: %r" % (entry,),
            )

    def test_no_wrapper_literal_in_allowlist(self):
        """Allowlist carries no new wrapper literal (FR-TESTING-006)."""
        self.assertNotIn(WRAPPER_LITERAL, self.require_allowlist())

    def test_no_wrapper_literal_in_gate(self):
        """Gate source carries no new wrapper literal (FR-TESTING-006)."""
        self.assertTrue(
            os.path.isfile(SCRIPT),
            "T7 not implemented: testing/determinism/dry_run_scope_gate.py missing",
        )
        self.assertNotIn(WRAPPER_LITERAL, read_source(SCRIPT))


class CheckScopeTest(unittest.TestCase):
    """check_scope: [] clean | violations name the path | fail closed."""

    def require_module(self):
        self.assertIsNotNone(
            gate,
            "T7 not implemented: testing.determinism.dry_run_scope_gate missing",
        )
        return gate

    def test_clean_tree_passes(self):
        """Tree with no doubles -> [] (AC-TESTING-036 happy path)."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "src", "worker.py"), "VALUE = 1\n")
            allowlist = write_allowlist(tree, ["tests", "testing/golden"])
            self.assertEqual(mod.check_scope(tree, allowlist), [])

    def test_empty_tree_passes(self):
        """Empty tree holds no double -> [] (boundary; ASSUMPTION)."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            allowlist = write_allowlist(tree, ["tests"])
            self.assertEqual(mod.check_scope(tree, allowlist), [])

    def test_double_under_production_dir_flagged(self):
        """Double-named file outside the allowlist fails naming it."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            bad = touch(os.path.join(tree, "src", "mock_worker.py"))
            allowlist = write_allowlist(tree, ["tests", "testing/golden"])
            violations = mod.check_scope(tree, allowlist)
            self.assertTrue(violations, "production double must be flagged")
            self.assertTrue(
                any("mock_worker" in v for v in violations),
                "violation must name the path: %r" % (violations,),
            )

    def test_double_under_allowlisted_unit_test_dir_passes(self):
        """Double inside an allowlisted unit-test dir is legitimate."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "tests", "mock_worker.py"))
            allowlist = write_allowlist(tree, ["tests", "testing/golden"])
            self.assertEqual(mod.check_scope(tree, allowlist), [])

    def test_double_under_allowlisted_dry_run_dir_passes(self):
        """Double inside an allowlisted dry-run path is legitimate."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "testing", "dry-run", "fake_engine.py"))
            allowlist = write_allowlist(
                tree, ["tests", "testing/golden", "testing/dry-run"]
            )
            self.assertEqual(mod.check_scope(tree, allowlist), [])

    def test_double_symbol_inside_py_file_flagged(self):
        """Double-like symbol in a production .py file fails naming it."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            touch(
                os.path.join(tree, "src", "worker.py"),
                "class MockWorker:\n    pass\n",
            )
            allowlist = write_allowlist(tree, ["tests", "testing/golden"])
            violations = mod.check_scope(tree, allowlist)
            self.assertTrue(violations, "double symbol must be flagged")
            self.assertTrue(
                any("worker.py" in v for v in violations),
                "violation must name the path: %r" % (violations,),
            )

    def test_shipped_artifact_under_dry_run_dir_flagged(self):
        """A shipped artifact under a dry-run path fails (AC-TESTING-037)."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "testing", "dry-run", "libengine.so"))
            allowlist = write_allowlist(
                tree, ["tests", "testing/golden", "testing/dry-run"]
            )
            violations = mod.check_scope(tree, allowlist)
            self.assertTrue(violations, "shipped artifact must be flagged")
            self.assertTrue(
                any("libengine.so" in v for v in violations),
                "violation must name the path: %r" % (violations,),
            )

    def test_missing_scope_raises(self):
        """Missing scope tree fails closed (ASSUMPTION: FileNotFoundError)."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            allowlist = write_allowlist(tree, ["tests"])
            with self.assertRaises(FileNotFoundError):
                mod.check_scope(os.path.join(tree, "does-not-exist-xyz"), allowlist)

    def test_missing_allowlist_raises(self):
        """Missing allowlist fails closed (ASSUMPTION: FileNotFoundError)."""
        mod = self.require_module()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "src", "worker.py"))
            with self.assertRaises(FileNotFoundError):
                mod.check_scope(tree, os.path.join(tree, "no-allowlist-xyz.txt"))


class DryRunScopeGateCLITest(unittest.TestCase):
    """CLI: exit 0 clean | exit 1 violation/fail-closed | exit 2 usage."""

    def require_script(self):
        self.assertTrue(
            os.path.isfile(SCRIPT),
            "T7 not implemented: testing/determinism/dry_run_scope_gate.py missing",
        )

    def test_main_clean_tree_exit_0(self):
        """main() on a clean tree returns 0 (AC-TESTING-036 happy path)."""
        mod = gate
        self.assertIsNotNone(mod, "T7 not implemented: gate module missing")
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "src", "worker.py"), "VALUE = 1\n")
            allowlist = write_allowlist(tree, ["tests"])
            self.assertEqual(mod.main(["--scope", tree, "--allowlist", allowlist]), 0)

    def test_main_production_double_exit_1(self):
        """main() with a production double returns 1 naming the path."""
        mod = gate
        self.assertIsNotNone(mod, "T7 not implemented: gate module missing")
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "src", "mock_worker.py"))
            allowlist = write_allowlist(tree, ["tests"])
            self.assertEqual(mod.main(["--scope", tree, "--allowlist", allowlist]), 1)

    def test_cli_clean_tree_exit_0(self):
        """CLI on a clean tree exits 0."""
        self.require_script()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "src", "worker.py"), "VALUE = 1\n")
            allowlist = write_allowlist(tree, ["tests"])
            proc = run_cli("--scope", tree, "--allowlist", allowlist)
            self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_cli_production_double_exit_1_naming_path(self):
        """CLI with a production double exits 1 naming the path."""
        self.require_script()
        with tempfile.TemporaryDirectory() as tree:
            touch(os.path.join(tree, "src", "mock_worker.py"))
            allowlist = write_allowlist(tree, ["tests"])
            proc = run_cli("--scope", tree, "--allowlist", allowlist)
            self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
            self.assertIn("mock_worker", proc.stdout + proc.stderr)

    def test_cli_missing_scope_exit_1(self):
        """CLI with a missing scope is exit 1 (fail closed), not 0 or 2."""
        self.require_script()
        with tempfile.TemporaryDirectory() as tree:
            allowlist = write_allowlist(tree, ["tests"])
            proc = run_cli(
                "--scope",
                os.path.join(tree, "does-not-exist-xyz"),
                "--allowlist",
                allowlist,
            )
            self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_cli_usage_error_exits_2(self):
        """Missing --scope is a usage error -> exit 2."""
        self.require_script()
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


class GateHermeticTest(unittest.TestCase):
    """Gate is stdlib-only: no network/device, no downloads, no secrets."""

    def test_gate_source_has_no_network_imports(self):
        """Gate imports no network surface (hermetic constraint)."""
        self.assertTrue(
            os.path.isfile(SCRIPT),
            "T7 not implemented: testing/determinism/dry_run_scope_gate.py missing",
        )
        roots = imported_modules(read_source(SCRIPT))
        for forbidden in FORBIDDEN_IMPORTS:
            root = forbidden.split(".")[0]
            self.assertNotIn(
                root, roots, "gate must not import %r (hermetic)" % (forbidden,)
            )


if __name__ == "__main__":
    unittest.main()
