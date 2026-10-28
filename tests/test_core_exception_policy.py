"""Batch 2 (T4) — -fno-exceptions render path with hard-failure boundary.

Covers NFR-CORE-006 AC-CORE-008 / TC-CORE-008
(``./gradlew :engine:core:exceptionPolicy``).

Public boundary under test (plan Checklist T4 Interface line,
reproduced verbatim)::

    [[noreturn]] void renderPathViolation(const char* reason) noexcept; + build contract: render-path TUs compile with -fno-exceptions; crossing an exception through the render boundary routes to renderPathViolation (abort with CORE_EXCEPTION_ESCAPE diagnostic). Error shape: any throw crossing the boundary → process abort in tests, never an exception object.

The sources under test are ``engine/core/exception_policy.h``,
``engine/core/render_path_tu.cpp`` and the build fragment
``cmake/PluginVisibility.cmake`` (or the split-out
``cmake/RenderPathFlags.cmake`` when the fragment must split), created
by the coder in T4. Assertions are static contract checks over those
files (existence + declared-interface tokens + abort/flag markers + a
no-``throw`` scan of the render-path TU); live abort-on-escape execution
is the coder's T4 build work and runs under
``:engine:core:exceptionPolicy`` (marked EXTERNAL below — the repo's
``gradlew`` shim currently exposes only the ``:benchmark:*`` and
``:engine:core:allocator`` surface). Hermetic: stdlib unittest only, no
network/secrets/services/config, no compilation.
"""

import os
import re
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE_ROOT = os.path.join(REPO_ROOT, "engine", "core")
POLICY_H = os.path.join(CORE_ROOT, "exception_policy.h")
RENDER_TU = os.path.join(CORE_ROOT, "render_path_tu.cpp")
CMAKE_PRIMARY = os.path.join(REPO_ROOT, "cmake", "PluginVisibility.cmake")
CMAKE_SPLIT = os.path.join(REPO_ROOT, "cmake", "RenderPathFlags.cmake")

THROW_RE = re.compile(r"(?<![\w:])throw\b(?![\w])")


def _read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def cmake_fragment_path():
    """Return the T4 build fragment, preferring the primary name."""
    for path in (CMAKE_PRIMARY, CMAKE_SPLIT):
        if os.path.isfile(path):
            return path
    return CMAKE_PRIMARY


def scan_throw_escapes(path):
    """Return 1-based line numbers in ``path`` containing a ``throw``.

    Comment and preprocessor-directive lines are not escapes.
    """
    hits = []
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, start=1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("#"):
                continue
            if THROW_RE.search(line):
                hits.append(lineno)
    return hits


class PolicyFilesExist(unittest.TestCase):
    def test_exception_policy_header_exists(self):
        self.assertTrue(
            os.path.isfile(POLICY_H),
            "engine/core/exception_policy.h must exist",
        )

    def test_render_path_tu_exists(self):
        self.assertTrue(
            os.path.isfile(RENDER_TU),
            "engine/core/render_path_tu.cpp must exist",
        )

    def test_build_fragment_exists(self):
        self.assertTrue(
            os.path.isfile(CMAKE_PRIMARY) or os.path.isfile(CMAKE_SPLIT),
            "cmake/PluginVisibility.cmake (or cmake/RenderPathFlags.cmake) must exist",
        )


class PolicyContract(unittest.TestCase):
    def test_render_path_violation_declared(self):
        text = _read(POLICY_H)
        self.assertTrue(
            re.search(r"renderPathViolation\s*\(\s*const\s+char\s*\*", text),
            "exception_policy.h must declare "
            "renderPathViolation(const char* reason)",
        )

    def test_render_path_violation_noreturn(self):
        text = _read(POLICY_H)
        self.assertTrue(
            re.search(r"\[\[noreturn\]\].*renderPathViolation", text, re.DOTALL),
            "renderPathViolation must be [[noreturn]]",
        )

    def test_render_path_violation_noexcept(self):
        text = _read(POLICY_H)
        self.assertTrue(
            re.search(r"renderPathViolation\s*\([^)]*\)\s*noexcept", text),
            "renderPathViolation must be noexcept",
        )

    def test_exception_escape_diagnostic_marker(self):
        found = False
        for path in (POLICY_H, RENDER_TU):
            if os.path.isfile(path) and "CORE_EXCEPTION_ESCAPE" in _read(path):
                found = True
        self.assertTrue(
            found,
            "the render boundary must emit the CORE_EXCEPTION_ESCAPE diagnostic",
        )


class RenderPathContract(unittest.TestCase):
    def test_render_tu_includes_policy(self):
        text = _read(RENDER_TU)
        self.assertIn(
            "exception_policy",
            text,
            "render_path_tu.cpp must include the exception policy",
        )

    def test_render_tu_routes_escape_to_violation(self):
        text = _read(RENDER_TU)
        self.assertIn(
            "renderPathViolation",
            text,
            "render_path_tu.cpp must route boundary escapes to renderPathViolation",
        )

    def test_render_tu_contains_no_throw(self):
        self.assertEqual(
            [],
            scan_throw_escapes(RENDER_TU),
            "render_path_tu.cpp must contain no throw",
        )


class BuildFlagsContract(unittest.TestCase):
    def test_fragment_sets_no_exceptions(self):
        text = _read(cmake_fragment_path())
        self.assertIn(
            "-fno-exceptions",
            text,
            "the build fragment must compile render-path TUs with -fno-exceptions",
        )

    def test_fragment_scopes_flag_to_render_path(self):
        text = _read(cmake_fragment_path())
        self.assertTrue(
            re.search(r"render_path|RenderPath|RENDER_PATH", text),
            "the -fno-exceptions flag must be scoped to the render-path TU",
        )


class BoundaryScannerDenialCases(unittest.TestCase):
    """Harness-only checks: the throw scanner used above must fail on an
    escaping render TU and pass on a clean one (tmp fixtures only)."""

    def _scan_text(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "render_tu.cpp")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
            return scan_throw_escapes(path)

    def test_throw_crossing_boundary_must_fail(self):
        hits = self._scan_text(
            "void frame() noexcept {\n"
            "  try { decode(); } catch (...) { throw; }\n"
            "}\n"
        )
        self.assertTrue(
            hits, "a throw crossing the render boundary must be flagged"
        )

    def test_direct_throw_on_render_path_must_fail(self):
        hits = self._scan_text("void frame() {\n  throw FrameError{};\n}\n")
        self.assertTrue(hits, "a direct throw on the render path must be flagged")

    def test_violation_route_must_pass(self):
        hits = self._scan_text(
            "#include \"exception_policy.h\"\n"
            "void frame() noexcept {\n"
            "  renderPathViolation(\"CORE_EXCEPTION_ESCAPE\");\n"
            "}\n"
        )
        self.assertEqual([], hits)

    def test_commented_throw_is_not_an_escape(self):
        hits = self._scan_text("// throw is forbidden here\nvoid frame() noexcept {}\n")
        self.assertEqual([], hits)


if __name__ == "__main__":
    unittest.main()
