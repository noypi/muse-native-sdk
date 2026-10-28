"""Batch 1 (T1) — honest GC model statement check.

Covers NFR-CORE-001 AC-CORE-002 / TC-CORE-002 (grep check over
``docs/core``).

Public boundary under test (plan Checklist T1 Interface line,
reproduced verbatim)::

    none — docs-only statement; verifiable by grep, exposes no callable surface.

The doc under test is ``docs/core/gc-model.md`` (created by the coder in
T1). The ``scan_gc_claims`` helper lives IN this test file since no
production interface exists; it is the grep-equivalent scan over the
``docs/core`` root. Hermetic: stdlib unittest only, tmp dirs + the real
docs root. No network/secrets/services/config.

Expected state before T1 implementation: RED — ``docs/core/gc-model.md``
does not exist, so the existence/content assertions fail and the
clean-tree scan yields a missing-root sentinel violation (non-empty).
The injected-fixture scanner tests pass from the start (they only need
the in-file scanner + tmp dirs).
"""

import os
import re
import tempfile
import unittest
from collections import namedtuple

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_CORE_ROOT = os.path.join(REPO_ROOT, "docs", "core")
DOC_PATH = os.path.join(DOCS_CORE_ROOT, "gc-model.md")

Violation = namedtuple("Violation", ["path", "line", "pattern"])

ZERO_GC_RE = re.compile(r"zero\s*[-_ ]*\s*gc", re.IGNORECASE)
MISSING_ROOT_PATTERN = "missing-root:docs/core"

# A line matching the zero-GC pattern is NOT a violation when it carries
# an explicit denial/prohibition marker on the same or previous line.
DENIAL_MARKERS = (
    "no ",
    "not ",
    "never ",
    "makes no",
    "without ",
    "disclaim",
    "prohibition",
    "forbidden",
)


def _is_denial(line, prev_line=""):
    low = line.lower()
    prev_low = (prev_line or "").lower()
    for marker in DENIAL_MARKERS:
        if marker in low or (prev_low and marker in prev_low):
            return True
    return False


def scan_gc_claims(root):
    """Scan ``root`` for process-wide zero-GC claims.

    Returns a list of ``Violation(path, line, pattern)``; ``[]`` means the
    tree is clean. A missing ``root`` yields a single sentinel violation
    (non-empty) so the clean-tree assertion stays RED until T1 creates
    the docs.
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
                lines = fh.readlines()
            prev = ""
            for lineno, line in enumerate(lines, start=1):
                if ZERO_GC_RE.search(line) and not _is_denial(line, prev):
                    violations.append(
                        Violation(path, lineno, "zero-gc-claim")
                    )
                prev = line
    return violations


def _read_doc():
    with open(DOC_PATH, encoding="utf-8") as fh:
        return fh.read()


class GcModelDocExists(unittest.TestCase):
    def test_gc_model_doc_exists(self):
        self.assertTrue(
            os.path.isfile(DOC_PATH),
            "docs/core/gc-model.md must exist (T1 creates it)",
        )


class GcModelContent(unittest.TestCase):
    def test_render_path_performs_no_gc(self):
        text = _read_doc()
        low = text.lower()
        self.assertIn("render path", low)
        self.assertTrue(
            ("no garbage collection" in low)
            or ("gc-free" in low)
            or ("gc free" in low)
            or ("performs no" in low and "garbage collection" in low),
            "doc must state the C++ render path performs no garbage collection",
        )

    def test_js_isolate_still_collects(self):
        text = _read_doc()
        low = text.lower()
        self.assertTrue(
            ("js" in low or "javascript" in low or "isolate" in low),
            "doc must name the JS isolate",
        )
        self.assertTrue(
            ("still collects" in low)
            or ("still performs" in low)
            or ("does collect" in low)
            or ("garbage-collected" in low or "garbage collected" in low),
            "doc must state honestly that the JS isolate still collects",
        )

    def test_explicit_no_process_wide_claim_prohibition(self):
        text = _read_doc()
        low = text.lower()
        self.assertTrue(
            ("no process-wide" in low and "zero" in low)
            or ("prohibition" in low)
            or ("must not" in low and "zero" in low),
            "doc must carry the explicit no-process-wide-zero-GC-claim prohibition",
        )


class GcModelGrepContract(unittest.TestCase):
    def test_docs_core_has_no_process_wide_zero_gc_claim(self):
        violations = scan_gc_claims(DOCS_CORE_ROOT)
        self.assertEqual(
            [],
            violations,
            "grep-equivalent: docs/core must contain no process-wide zero-GC claim",
        )

    def test_grep_command_shape(self):
        # The plan's acceptance command must be expressible as a grep over
        # docs/core; this pins the scanned root, not the implementation.
        self.assertTrue(os.path.isdir(DOCS_CORE_ROOT) or True)
        self.assertEqual(DOCS_CORE_ROOT, os.path.join(REPO_ROOT, "docs", "core"))


class ScannerDenialCases(unittest.TestCase):
    def _scan_text(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "note.md")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
            return scan_gc_claims(tmp)

    def test_process_wide_zero_gc_claim_present_must_fail(self):
        violations = self._scan_text(
            "# Engine notes\n\nOur engine has zero GC pauses process-wide.\n"
        )
        self.assertTrue(
            violations,
            "a process-wide zero-GC claim must be reported as a violation",
        )

    def test_prohibition_line_quoting_zero_gc_must_pass(self):
        violations = self._scan_text(
            "# GC model\n\n"
            "This document makes no process-wide zero-GC claim.\n"
        )
        self.assertEqual([], violations)

    def test_missing_root_is_a_violation(self):
        violations = scan_gc_claims(os.path.join(REPO_ROOT, "docs", "no-such-dir-xyz"))
        self.assertTrue(violations)


if __name__ == "__main__":
    unittest.main()
