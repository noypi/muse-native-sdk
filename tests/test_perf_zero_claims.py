"""Batch 3 (T6) — Zero-absolute-claim doc scan check.

Covers NFR-PERFORMANCE-004 AC-004 / TC-004 / TC-025 (grep checks).

The scanner ``scan_zero_claims`` lives IN this test file since no
production interface exists for it (plan Checklist T6 Interface line);
it is a grep-equivalent scan over the ``docs/performance`` root created
by T5. Hermetic: stdlib unittest only, tmp dirs + the real docs root.
No network/secrets/services.

Public boundary under test (plan Checklist T6 Interface line,
reproduced verbatim)::

    scan_zero_claims(root: str) -> list[Violation(path: str, line: int, pattern: str)]

Expected state before T5 implementation: RED — ``docs/performance``
does not exist yet, so the docs-root-exists assertion fails and the
clean-tree scan assertion fails (missing root yields a sentinel
violation, i.e. non-empty). The injected-fixture tests pass from the
start (they only need the in-file scanner + tmp dirs).
"""

import os
import re
import tempfile
import unittest
from collections import namedtuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_PERF_ROOT = os.path.join(REPO_ROOT, "docs", "performance")

Violation = namedtuple("Violation", ["path", "line", "pattern"])

FORBIDDEN_PATTERNS = (
    r"zero\s*[-_]*\s*gc",
    r"zero\s*[-_]*\s*cost",
    r"zero\s*[-_]*\s*latency",
)
FORBIDDEN_RES = tuple(re.compile(p, re.IGNORECASE) for p in FORBIDDEN_PATTERNS)

MISSING_ROOT_PATTERN = "missing-root:docs/performance"


DENIAL_MARKERS = ("no ", "not ", "never ", "makes no", "without ", "disclaim")


def _is_allowlisted(line, prev_line=""):
    """Allowlist (plan T6): honest bounded render-path phrasing only.

    - ``GC-free`` render-path phrasing (the honest render-path GC-free /
      Hermes-isolate-GC split T5 writes);
    - the ``docs/state/api-surface.md`` PROHIBITION line pattern
      (explicit honest-split language: a line carrying PROHIBITION that
      quotes the forbidden phrase to forbid it);
    - explicit-DENIAL phrasing: a line matching the absolute-claim
      pattern is NOT a violation when the same line (or the immediately
      preceding line) carries an explicit denial marker (``no ``,
      ``not ``, ``never ``, ``makes no``, ``without ``, ``disclaim``,
      case-insensitive), e.g. "this document makes no process-wide
      zero-GC claim".
    """
    low = line.lower()
    if "gc-free" in low or "gc free" in low:
        return True
    if "prohibition" in low and "never describe" in low:
        return True
    prev_low = (prev_line or "").lower()
    for marker in DENIAL_MARKERS:
        if marker in low or (prev_low and marker in prev_low):
            return True
    return False


def scan_zero_claims(root):
    """Scan ``root`` for absolute process-wide claims.

    Returns a list of ``Violation(path, line, pattern)``; ``[]`` means
    the tree is clean. A missing ``root`` yields a single sentinel
    violation (non-empty) so the clean-tree assertion stays RED until
    T5 creates the docs.
    """
    if not os.path.isdir(root):
        return [Violation(root, 0, MISSING_ROOT_PATTERN)]
    violations = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            if not name.endswith(".md"):
                continue
            path = os.path.join(dirpath, name)
            with open(path, encoding="utf-8") as fh:
                prev_line = ""
                for lineno, line in enumerate(fh, start=1):
                    for rx in FORBIDDEN_RES:
                        m = rx.search(line)
                        if m and not _is_allowlisted(line, prev_line):
                            violations.append(
                                Violation(path, lineno, m.group(0))
                            )
                            break
                    prev_line = line
    return violations


def _write(tmpdir, relpath, text):
    path = os.path.join(tmpdir, relpath)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


class DocsRootExistsTest(unittest.TestCase):
    def test_docs_performance_root_exists(self):
        # T5 creates docs/performance; absent now -> RED.
        self.assertTrue(
            os.path.isdir(DOCS_PERF_ROOT),
            msg="docs/performance must exist (created by T5)",
        )


class CleanTreeScanTest(unittest.TestCase):
    def test_scan_passes_on_t5_docs(self):
        # AC-004/TC-004/TC-025: clean honest docs -> [].
        # RED now: root missing -> sentinel violation (non-empty).
        self.assertEqual(scan_zero_claims(DOCS_PERF_ROOT), [])

    def test_scan_error_shape_is_path_line_pattern(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "a.md", "this render is zero-cost everywhere\n")
            violations = scan_zero_claims(tmp)
            self.assertEqual(len(violations), 1)
            v = violations[0]
            self.assertTrue(v.path.endswith("a.md"))
            self.assertEqual(v.line, 1)
            self.assertIn("zero", v.pattern.lower())


class AbsoluteClaimVariantTest(unittest.TestCase):
    def test_flags_zero_gc_variant(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "c.md", "# Honest\n\nWe achieve zero GC pauses.\n")
            violations = scan_zero_claims(tmp)
            self.assertEqual(len(violations), 1)
            self.assertEqual(violations[0].line, 3)

    def test_flags_zero_cost_variant(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "c.md", "zero-cost rendering for all frames\n")
            self.assertTrue(scan_zero_claims(tmp))

    def test_flags_zero_latency_variant(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "c.md", "our pipeline has zero-latency dispatch\n")
            self.assertTrue(scan_zero_claims(tmp))

    def test_flags_case_insensitive_zero_space_gc(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "c.md", "Zero GC overhead process-wide\n")
            self.assertTrue(scan_zero_claims(tmp))

    def test_injected_absolute_claim_fixture_is_flagged(self):
        # Plan T6 red-green: injected fixture the scan must flag.
        # Passes from the start (scanner only, hermetic tmp dir).
        with tempfile.TemporaryDirectory() as tmp:
            _write(
                tmp,
                "fixture.md",
                "# Fixture\n\nThis engine has zero-cost dispatch.\n",
            )
            violations = scan_zero_claims(tmp)
            self.assertTrue(
                violations,
                msg="scan must flag the injected absolute-claim fixture",
            )
            self.assertIn("zero", violations[0].pattern.lower())


class AllowlistTest(unittest.TestCase):
    def test_gc_free_render_path_phrasing_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(
                tmp,
                "ok.md",
                "The C++ render path is allocation-stable and GC-free"
                " after bootstrap.\n",
            )
            self.assertEqual(scan_zero_claims(tmp), [])

    def test_prohibition_line_pattern_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(
                tmp,
                "ok.md",
                'PROHIBITION: Never describe JS-authored logic as "zero GC".\n',
            )
            self.assertEqual(scan_zero_claims(tmp), [])

    def test_denial_phrasing_same_line_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(
                tmp,
                "ok.md",
                "This document makes no process-wide zero-GC claim.\n",
            )
            self.assertEqual(scan_zero_claims(tmp), [])

    def test_denial_marker_preceding_line_allowed(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(
                tmp,
                "ok.md",
                "We make no absolute claim here.\n"
                "The following phrase is disclaimed: zero-cost dispatch.\n",
            )
            self.assertEqual(scan_zero_claims(tmp), [])

    def test_unqualified_claim_with_denial_elsewhere_still_flagged(self):
        # Denial allowlist is line-scoped (same or immediately preceding
        # line only): a denial two lines away must NOT suppress the flag.
        with tempfile.TemporaryDirectory() as tmp:
            _write(
                tmp,
                "bad.md",
                "We make no absolute claim here.\n"
                "\n"
                "This engine has zero-cost dispatch.\n",
            )
            self.assertTrue(scan_zero_claims(tmp))


class MissingRootSentinelTest(unittest.TestCase):
    def test_missing_root_returns_sentinel_violation(self):
        # Fail-closed: nonexistent root -> single `missing-root:<name>`
        # sentinel violation (non-empty).
        with tempfile.TemporaryDirectory() as tmp:
            missing = os.path.join(tmp, "does-not-exist-xyz")
            violations = scan_zero_claims(missing)
            self.assertTrue(violations, msg="missing root must fail closed")
            self.assertEqual(len(violations), 1)
            self.assertIn("missing-root:", violations[0].pattern)
            self.assertIn("does-not-exist-xyz", violations[0].path)


class ScanScopeTest(unittest.TestCase):
    def test_non_markdown_files_not_scanned(self):
        with tempfile.TemporaryDirectory() as tmp:
            _write(tmp, "note.txt", "zero-cost everything\n")
            self.assertEqual(scan_zero_claims(tmp), [])

    def test_out_of_scope_engine_comment_not_in_docs_root(self):
        # Documented plan note: engine/layout/tests/delegate_test.cpp
        # "zero-cost" comment is out of the scanned root (not published
        # material) — the scan root is docs/performance only.
        self.assertNotIn("engine", DOCS_PERF_ROOT)


if __name__ == "__main__":
    unittest.main()
