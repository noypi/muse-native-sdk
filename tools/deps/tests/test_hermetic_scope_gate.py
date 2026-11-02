#!/usr/bin/env python3
"""Batch 1 (T2) hermetic-scope source gate test (TC-TESTING-036 upgrade).

Covers plan.md T2 (plan Interface:
`check_hermetic_scope(src_roots: list[str]) -> list[str]` — returns
violations for stub/fake bodies outside `tests/`/`dry-run` fences
(allowlist: `#ifdef HOST_SCAFFOLD` + milestone)).

What this asserts (hermetic, stdlib `unittest` + `os`/`re` only):
- The scope rule itself, against inline tmp trees: a double body in an
  unfenced prod file is a violation; the same body under `tests/` or
  `dry-run/`, or behind `#ifdef HOST_SCAFFOLD` *with a dated removal
  milestone*, is allowed; a `HOST_SCAFFOLD` fence with no milestone is
  still a violation.
- The live tree: the four support.md leak paths are flagged
  (`js_runtime.h/.cpp`, `js_engine.cpp`, `egl_blit.h`,
  `present_skia.cpp`, `jni_bridge.cpp` stubbed defaults, shaper +
  font-discovery stubs, `vulkan_baseline.h` fake vocabulary).

RED now: the live-tree case fails naming the four paths. It turns green
when T4/T6/T8 land (or when a kept host scaffold carries the labeled
`HOST_SCAFFOLD` fence + dated milestone). The rule cases stay green
throughout.

Hermetic: reads only the committed checkout tree plus `tempfile` trees
owned by the test process. No network, no secrets, no application
config, no live service, no subprocess.

Per GR-08 T1.8 this file is assigned to T2 and mapped in
workflow.local/runs/bug-runtime-008/unit-tests/T2.md.
"""

import os
import re
import tempfile
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)

# Double vocabulary compiled into production sources.
_DOUBLE_PATTERNS = (
    r"\bFake\w*",
    r"\bStub\w*",
    r"\bMock\w*",
    r"\bstubbed defaults\b",
    r"\bstubbed data\b",
    r"\bstubbed enumerator\b",
    r"\bstub\b.*\bplaceholder\b",
    r"\bFallbackAdvance\b",  # deterministic shaper stub body (path 2 host half)
)
_DOUBLE_RE = re.compile("|".join(_DOUBLE_PATTERNS))

# Fence segments: file is out of scope when under one of these, or when it
# carries a HOST_SCAFFOLD fence with a dated removal milestone.
_FENCE_DIR_RE = re.compile(r"(^|/)(tests|dry-run)(/|$)")
_SCAFFOLD_RE = re.compile(r"#ifdef\s+HOST_SCAFFOLD")
_MILESTONE_RE = re.compile(
    r"(removal milestone|REMOVE BY|remove-by)\s*:?\s*\S+", re.IGNORECASE
)

_SOURCE_EXTENSIONS = (".cpp", ".h", ".hpp", ".cc")

# Production roots the T2 gate scans (support.md four leak paths live here).
_SRC_ROOTS = (
    "engine/runtime/src",
    "engine/runtime/include",
    "engine/text/src",
    "engine/text/include",
    "engine/graphics/skia",
)


def _is_fenced(relpath, text):
    if _FENCE_DIR_RE.search(relpath):
        return True
    if _SCAFFOLD_RE.search(text) and _MILESTONE_RE.search(text):
        return True
    return False


def check_hermetic_scope(src_roots, repo_root=_REPO_ROOT):
    """Plan Interface under test (source-scan half).

    `src_roots`: repo-relative production roots to scan. Returns
    violation strings; an empty or missing root fails closed.
    """
    if not src_roots:
        return [
            "check_hermetic_scope: deny: no src roots supplied; "
            "failing closed"
        ]
    violations = []
    for root in src_roots:
        abspath = os.path.join(repo_root, root)
        if not os.path.isdir(abspath):
            violations.append(
                "check_hermetic_scope: deny: src root %r missing; "
                "failing closed" % root
            )
            continue
        for dirpath, _dirnames, filenames in os.walk(abspath):
            for filename in filenames:
                if not filename.endswith(_SOURCE_EXTENSIONS):
                    continue
                path = os.path.join(dirpath, filename)
                relpath = os.path.relpath(path, repo_root)
                try:
                    with open(path, "r", encoding="utf-8") as handle:
                        text = handle.read()
                except (OSError, UnicodeDecodeError):
                    continue
                match = _DOUBLE_RE.search(text)
                if match and not _is_fenced(relpath, text):
                    violations.append(
                        "check_hermetic_scope: deny: test-double "
                        "vocabulary %r in production source %s"
                        % (match.group(0), relpath)
                    )
    return violations


def _write_tree(files):
    tmp = tempfile.mkdtemp(prefix="hermetic-scope-")
    for relpath, text in files.items():
        path = os.path.join(tmp, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    return tmp


class HermeticScopeRuleTest(unittest.TestCase):
    def _check(self, files, roots=("src",)):
        return check_hermetic_scope(roots, repo_root=_write_tree(files))

    def test_unfenced_fake_body_in_prod_is_violation(self):
        violations = self._check(
            {"src/js_runtime.cpp": "class FakeJsRuntime : public JsRuntime {};\n"}
        )
        self.assertTrue(violations)
        self.assertTrue(any("js_runtime.cpp" in v for v in violations))

    def test_stubbed_defaults_in_prod_are_violation(self):
        violations = self._check(
            {"src/jni_bridge.cpp": "// stubbed defaults — no native preference\n"}
        )
        self.assertTrue(violations)

    def test_same_body_under_tests_is_allowed(self):
        violations = self._check(
            {"src/tests/fake_engine.cpp": "class FakeJsRuntime {};\n"}
        )
        self.assertEqual(violations, [])

    def test_same_body_under_dry_run_is_allowed(self):
        violations = self._check(
            {"src/dry-run/stub_egl.cpp": "bool StubCreateSurface() { return true; }\n"}
        )
        self.assertEqual(violations, [])

    def test_scaffold_fence_with_milestone_is_allowed(self):
        violations = self._check(
            {
                "src/egl_blit.h": (
                    "#ifdef HOST_SCAFFOLD\n"
                    "// removal milestone: next runtime-owner milestone\n"
                    "bool StubCreateSurface() { return true; }\n"
                    "#endif\n"
                )
            }
        )
        self.assertEqual(violations, [])

    def test_scaffold_fence_without_milestone_is_violation(self):
        violations = self._check(
            {
                "src/egl_blit.h": (
                    "#ifdef HOST_SCAFFOLD\n"
                    "bool StubCreateSurface() { return true; }\n"
                    "#endif\n"
                )
            }
        )
        self.assertTrue(violations)

    def test_empty_roots_fail_closed(self):
        violations = check_hermetic_scope([])
        self.assertTrue(violations)
        self.assertTrue(any("failing closed" in v for v in violations))

    def test_missing_root_fails_closed_naming_it(self):
        bad = "src/does-not-exist-xyz"
        root = _write_tree({"src/real.cpp": "int x = 1;\n"})
        violations = check_hermetic_scope([bad], repo_root=root)
        self.assertTrue(any(bad in v for v in violations))


class HermeticScopeLiveTreeTest(unittest.TestCase):
    def test_live_tree_has_no_unfenced_doubles_in_prod(self):
        violations = check_hermetic_scope(_SRC_ROOTS)
        self.assertEqual(
            violations, [],
            "test-double vocabulary in production sources:\n%s"
            % "\n".join(violations),
        )


if __name__ == "__main__":
    unittest.main()
