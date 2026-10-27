"""Batch 3 — Flat-map lint: held refs across mutation (T5).

Covers FR-DEPS-005 (AC-DEPS-011, AC-DEPS-012) per
specs-cpp-dependencies.md: no pointer/reference/iterator into a flat map
(``ankerl::unordered_dense``, ``absl::flat_hash_map``) may outlive an
insertion or erasure; identity crossing a mutation is an integer handle,
and a map that erases while other entries stay referenced is node-stable
(TC-DEPS-011 lint self-test; TC-DEPS-012 manual containers.md guidance).

Hermetic: stdlib unittest only, tmp ``.cpp`` fixtures scanned via
``--root``, no network/secrets/services.
Expected state before implementation: RED — ``tools/deps/flatmap_lint.py``
does not exist, so every CLI test fails (interpreter exit 2 / missing
file) and only the usage-error test can coincidentally pass; the
containers.md guidance test fails because the doc is absent.
"""

import os
import re
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "flatmap_lint.py")
CONTAINERS_DOC = os.path.join(REPO_ROOT, "docs", "deps", "containers.md")

HELD_REF_ERASE = """\
#include <string>
#include "ankerl/unordered_dense.h"
int lookup(ankerl::unordered_dense<int, std::string> &map, int k) {
  auto &ref = map[k];
  map.erase(k);
  return (int)ref.size();
}
"""

HELD_PTR_INSERT = """\
#include <string>
#include "ankerl/unordered_dense.h"
void grow(ankerl::unordered_dense<int, std::string> &map) {
  auto *p = &map[1];
  map[2] = "new";
  use(*p);
}
"""

HELD_ITER_ERASE = """\
#include <string>
#include "ankerl/unordered_dense.h"
void drop(ankerl::unordered_dense<int, std::string> &map) {
  auto it = map.find(1);
  map.erase(1);
  use(it->second);
}
"""

CLEAN_NO_FLATMAP = """\
int add(int a, int b) { return a + b; }
"""

HANDLE_IDENTITY_PASS = """\
#include <string>
#include "ankerl/unordered_dense.h"
// Identity crossing the mutation is an integer handle (RULE-DEPS-006).
int lookup(ankerl::unordered_dense<int, std::string> &map, int id) {
  auto copy = map[id];
  map.erase(id);
  return (int)copy.size();
}
"""


def run_cli(*args):
    """Run the lint CLI hermetically; return CompletedProcess."""
    return subprocess.run(
        [sys.executable, SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )


def write_cpp(tmpdir, name, text):
    """Write a ``.cpp`` fixture into tmpdir; return its path."""
    path = os.path.join(tmpdir, name)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


class TestFlatmapLint(unittest.TestCase):
    """T5 CLI: exit 0 clean | exit 1 violation/self-test fail | exit 2 usage."""

    def test_clean_source_passes(self):
        """A file with no flat map passes -> exit 0."""
        with tempfile.TemporaryDirectory() as tmpdir:
            write_cpp(tmpdir, "clean.cpp", CLEAN_NO_FLATMAP)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_handle_identity_passes(self):
        """Handle-identity code (value copy, no held ref) passes (AC-DEPS-011)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            write_cpp(tmpdir, "handle.cpp", HANDLE_IDENTITY_PASS)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_ref_across_erase_flagged(self):
        """``auto& ref = map[k]; ... map.erase(...)`` is flagged (AC-DEPS-011)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            write_cpp(tmpdir, "bad.cpp", HELD_REF_ERASE)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertRegex(combined, r"\S+:\d+")
        self.assertIn("AC-DEPS-011", combined)

    def test_ptr_across_insert_flagged(self):
        """A pointer held across an insertion is flagged (AC-DEPS-011)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            write_cpp(tmpdir, "bad.cpp", HELD_PTR_INSERT)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertRegex(combined, r"\S+:\d+")
        self.assertIn("AC-DEPS-011", combined)

    def test_iterator_across_erase_flagged(self):
        """An iterator held across an erasure is flagged (AC-DEPS-011)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            write_cpp(tmpdir, "bad.cpp", HELD_ITER_ERASE)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertRegex(combined, r"\S+:\d+")

    def test_violation_names_node_stable_guidance(self):
        """Violation output points at the node-stable remedy (AC-DEPS-012)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            write_cpp(tmpdir, "bad.cpp", HELD_REF_ERASE)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertTrue(
            "ac-deps-012" in combined or "node-stable" in combined,
            msg=proc.stdout + proc.stderr,
        )

    def test_violation_reports_file_and_line(self):
        """Violation output gives ``file:line`` for the offending hold."""
        with tempfile.TemporaryDirectory() as tmpdir:
            path = write_cpp(tmpdir, "bad.cpp", HELD_REF_ERASE)
            proc = run_cli("--root", tmpdir)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = proc.stdout + proc.stderr
        self.assertIn(os.path.basename(path), combined)
        self.assertRegex(combined, re.compile(r"bad\.cpp:\d+"))

    def test_missing_root_fails_closed(self):
        """A missing scan root is exit 1 (fail closed), not success."""
        proc = run_cli(
            "--root", os.path.join(REPO_ROOT, "does-not-exist-xyz")
        )
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_self_test_passes(self):
        """Bundled self-test passes (TC-DEPS-011)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)

    def test_containers_doc_states_handle_and_stability(self):
        """Container guidance states the handle rule + node-stable choice (TC-DEPS-012)."""
        self.assertTrue(
            os.path.isfile(CONTAINERS_DOC),
            msg="docs/deps/containers.md is absent",
        )
        with open(CONTAINERS_DOC, encoding="utf-8") as fh:
            text = fh.read()
        lowered = text.lower()
        self.assertIn("rule-deps-006", lowered)
        self.assertIn("handle", lowered)
        self.assertTrue(
            "node-stable" in lowered or "node stable" in lowered,
            msg="containers.md must state the node-stable choice",
        )


if __name__ == "__main__":
    unittest.main()
