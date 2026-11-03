#!/usr/bin/env python3
"""Batch 1 (T1) symbol-level prod-link gate test (TC-DEPS-019 upgrade).

Covers plan.md T1 (plan Interface:
`check_prod_symbols(lib_objects: list[str]) -> list[str]` — returns
violation strings naming `Fake|Stub|Mock` symbols in `runtime` objects;
fail-closed on missing objects).

What this asserts (hermetic, stdlib `unittest` + `os`/`re` only):
- No `Fake|Stub|Mock` *symbol definition* resolves from a production
  target: `FakeJsRuntime`, `JsRuntimeFactory::createFake`,
  `FakeEngineHooks`, `StubCreateSurface/MakeCurrent/SwapOnce`,
  `StubSkiaInit/MakeContext/SwapOnce`, plus test-double vocabulary
  compiled into prod headers (`Fake device capabilities`,
  `stubbed data` enumerator).
- Fail-closed: a missing object/symbol input is a violation, never a pass.
- The scan reads only the committed checkout tree (no `nm` device build,
  no network, no secrets, no application config, no live service).

RED now (Batch 4): the live tree still names the text-path doubles owned
by bug-text-007 (`FallbackAdvance`, `stubbed data/enumerator`), so the
live-tree case stays red for those paths only. The T7 fenced `Stub*`
scaffolds (`egl_blit.h`, `present_skia.cpp`) are allowlisted per the Q1
ruling — their unfenced arms fail closed — and the acceptance vocabulary is
pinned against a synthetic dirty tree (the `FakeJsRuntime` prod fixture was
deleted in T3). The coder wires the `nm`-based
`tools/deps/prod_symbol_check.py` gate with the plan Interface; this file is
reconciled to import it per GR-12).

Per GR-08 T1.8 this file is assigned to T1 and mapped in
workflow.local/runs/bug-runtime-008/unit-tests/T1.md.
"""

import os
import re
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)

# Double vocabulary compiled into production sources.
# Q1 ruling (Batch 4 review): fenced `Stub*` scaffolds are ALLOWLISTED, not
# deleted — unfenced bodies are fail-closed (`false` → CPU fallback). The
# scan below is fence-aware: a `Stub*` symbol is a violation only when its
# definition in the UNFENCED shape (the `#ifdef HOST_SCAFFOLD` true-arm
# removed) can answer `true`. `Fake|Mock|stubbed-data|FallbackAdvance`
# vocabulary is never allowlisted and matches anywhere in the file.
_STUB_DEF_RE = re.compile(
    r"(?:^|\n)\s*(?:inline\s+)?bool\s+(Stub\w*)\s*\(\s*\)\s*\{([^}]*)\}"
)
_RETURN_TRUE_RE = re.compile(r"return\s+true")
_RETURN_FALSE_RE = re.compile(r"return\s+false")


def _unfenced_text(text):
    """Return the file shape compiled WITHOUT `-DHOST_SCAFFOLD`.

    Strips each `#ifdef HOST_SCAFFOLD ... #else` true-arm, keeping the
    `#else ... #endif` fail-closed arm (or nothing when there is no
    `#else`). Files without the fence are returned unchanged.
    """
    out = []
    pos = 0
    for match in re.finditer(
        r"#ifdef\s+HOST_SCAFFOLD(.*?)#endif", text, re.DOTALL
    ):
        out.append(text[pos:match.start()])
        block = match.group(1)
        els = re.search(r"#else", block)
        out.append(block[els.end():] if els else "")
        pos = match.end()
    out.append(text[pos:])
    return "".join(out)


def _unfenced_stub_violations(relpath, text):
    """Violation strings for `Stub*` defs that stay `true` when unfenced."""
    violations = []
    unfenced = _unfenced_text(text)
    defs = {}
    for match in _STUB_DEF_RE.finditer(unfenced):
        defs.setdefault(match.group(1), []).append(match.group(2))
    for name, bodies in defs.items():
        if any(_RETURN_TRUE_RE.search(body) for body in bodies):
            violations.append(
                "check_prod_symbols: deny: %s symbol with unfenced "
                "`true` body resolves from production target %s"
                % (name, relpath)
            )
    return violations


# Production translation units / headers linked into the `runtime` static
# lib (support.md paths 1, 2-device, 4). Each entry maps a prod file to the
# double-symbol patterns that must NOT resolve from it.
_PROD_SYMBOL_TARGETS = {
    "engine/runtime/src/js_runtime.cpp": [
        r"\bclass\s+FakeJsRuntime\b",
        r"\bcreateFake\b",
        r"\bFakeJsRuntime\b",
    ],
    "engine/runtime/include/runtime/js_runtime.h": [
        r"\bFakeEngineHooks\b",
        r"\bcreateFake\b",
    ],
    "engine/runtime/src/js_engine.cpp": [
        r"\bFakeEngineHooks\b",
        r"\bcreateFake\b",
    ],
    "engine/runtime/include/runtime/egl_blit.h": [
        r"\bStubCreateSurface\b",
        r"\bStubMakeCurrent\b",
        r"\bStubSwapOnce\b",
    ],
    "engine/graphics/skia/present_skia.cpp": [
        r"\bStubSkiaInit\b",
        r"\bStubSkiaMakeContext\b",
        r"\bStubSkiaSwapOnce\b",
    ],
    "engine/runtime/include/runtime/vulkan_baseline.h": [
        r"\bFake device capabilities\b",
    ],
    "engine/text/src/shaper.cpp": [
        r"\bFallbackAdvance\b",
    ],
    "engine/text/include/muse/text/font_discovery.h": [
        r"\bstubbed data\b",
    ],
    "engine/text/src/font_discovery.cpp": [
        r"\bstubbed enumerator\b",
    ],
}

# Symbols the T1 acceptance names explicitly; the live-tree failure must
# name at least these.
_ACCEPTANCE_SYMBOLS = (
    "FakeJsRuntime",
    "createFake",
    "StubSkia",
    "StubCreateSurface",
)


def scan_prod_symbols(repo_root=_REPO_ROOT):
    """Return violation strings for double symbols in prod targets.

    Fail-closed: a missing/unreadable prod target yields a violation
    naming it (never a silent pass).
    """
    violations = []
    for relpath, patterns in _PROD_SYMBOL_TARGETS.items():
        path = os.path.join(repo_root, relpath)
        try:
            with open(path, "r", encoding="utf-8") as handle:
                text = handle.read()
        except (OSError, UnicodeDecodeError) as exc:
            violations.append(
                "check_prod_symbols: deny: production target %r "
                "unreadable (%s); failing closed" % (relpath, exc)
            )
            continue
        for pattern in patterns:
            if not re.search(pattern, text):
                continue
            if pattern.lstrip(r"\b").startswith("Stub"):
                violations.extend(
                    _unfenced_stub_violations(relpath, text)
                )
                break
            violations.append(
                    "check_prod_symbols: deny: %s symbol matching %r "
                    "resolves from production target %s"
                    % (pattern.strip(r"\b"), pattern, relpath)
                )
    return violations


def check_prod_symbols(lib_objects):
    """Plan Interface under test (source-level half).

    `lib_objects`: production symbol inputs to scan. An empty or
    missing input fails closed with a violation naming it.
    """
    if not lib_objects:
        return [
            "check_prod_symbols: deny: no production objects supplied; "
            "failing closed"
        ]
    missing = [
        obj for obj in lib_objects
        if not os.path.exists(os.path.join(_REPO_ROOT, obj))
    ]
    violations = [
        "check_prod_symbols: deny: production object %r missing; "
        "failing closed" % obj for obj in missing
    ]
    violations.extend(scan_prod_symbols())
    return violations


class ProdSymbolGateTest(unittest.TestCase):
    def test_no_fake_stub_symbols_in_runtime_objects(self):
        violations = scan_prod_symbols()
        self.assertEqual(
            violations, [],
            "Fake/Stub symbols resolve from production targets:\n%s"
            % "\n".join(violations),
        )

    def test_acceptance_symbols_are_named(self):
        # Fixture refreshed Batch 4 (MAJOR-1): `FakeJsRuntime` was deleted
        # from prod in T3, so the live tree no longer names it. The
        # acceptance vocabulary is pinned against a synthetic dirty tree
        # instead of the live tree — the gate must name every symbol here.
        violations = scan_prod_symbols(repo_root=_dirty_tree_root())
        joined = "\n".join(violations)
        for symbol in _ACCEPTANCE_SYMBOLS:
            self.assertIn(
                symbol, joined,
                "acceptance symbol %r not named by the gate" % symbol,
            )

    def test_empty_objects_fail_closed(self):
        violations = check_prod_symbols([])
        self.assertTrue(violations)
        self.assertTrue(any("failing closed" in v for v in violations))

    def test_missing_object_fails_closed_naming_it(self):
        bad = "engine/runtime/src/does-not-exist-xyz.cpp"
        violations = check_prod_symbols([bad])
        self.assertTrue(any(bad in v for v in violations))
        self.assertTrue(any("failing closed" in v for v in violations))

    def test_clean_tree_passes(self):
        violations = scan_prod_symbols(repo_root=_empty_tree_root())
        self.assertEqual(violations, [])

    def test_unfenced_stub_arms_fail_closed(self):
        """Security pin: without `-DHOST_SCAFFOLD` every stub is `false`.

        Guards against flag leakage into prod/device builds: the compiled-out
        shape of each fenced stub body must fail closed. Fail-closed test
        design: a file with no fenced stub defs fails here (vacuous pass
        denied), and any unfenced `return true` is a violation.
        """
        fenced = (
            "engine/runtime/include/runtime/egl_blit.h",
            "engine/graphics/skia/present_skia.cpp",
        )
        for relpath in fenced:
            path = os.path.join(_REPO_ROOT, relpath)
            try:
                with open(path, "r", encoding="utf-8") as handle:
                    text = handle.read()
            except OSError as exc:
                self.fail("production target %r unreadable (%s)" % (relpath, exc))
            self.assertIn(
                "#ifdef HOST_SCAFFOLD", text,
                "fence missing in %s; failing closed" % relpath,
            )
            unfenced = _unfenced_text(text)
            defs = list(_STUB_DEF_RE.finditer(unfenced))
            self.assertTrue(
                defs, "no unfenced stub defs in %s; failing closed" % relpath
            )
            for match in defs:
                self.assertIsNone(
                    _RETURN_TRUE_RE.search(match.group(2)),
                    "unfenced %s in %s can answer `true`" % (match.group(1), relpath),
                )
                self.assertIsNotNone(
                    _RETURN_FALSE_RE.search(match.group(2)),
                    "unfenced %s in %s is not fail-closed `false`"
                    % (match.group(1), relpath),
                )

    def test_no_prod_target_defines_host_scaffold(self):
        """LOW pin: `HOST_SCAFFOLD` must never reach a prod/device build.

        No committed build file may define the flag; the fenced suites pass
        it explicitly on the compiler command line only. A build-config leak
        would re-arm success-without-work stubs on device legs.
        """
        hits = []
        found_cmake = False
        for dirpath, _dirnames, filenames in os.walk(_REPO_ROOT):
            if ".git" in dirpath:
                continue
            for filename in filenames:
                if filename != "CMakeLists.txt" and not filename.endswith(".cmake"):
                    continue
                found_cmake = True
                path = os.path.join(dirpath, filename)
                try:
                    with open(path, "r", encoding="utf-8") as handle:
                        text = handle.read()
                except (OSError, UnicodeDecodeError):
                    continue
                if "HOST_SCAFFOLD" in text:
                    hits.append(os.path.relpath(path, _REPO_ROOT))
        self.assertTrue(found_cmake, "no build files scanned; failing closed")
        self.assertEqual(
            hits, [],
            "`HOST_SCAFFOLD` defined by committed build files:\n%s"
            % "\n".join(hits),
        )

    def test_prod_link_objects_contain_no_fake_symbols(self):
        """Linkage-level proof: no Fake TU is linked into prod targets.

        Parses the `runtime` static lib source list from
        `engine/runtime/CMakeLists.txt` (`add_library(runtime ...)`) and
        fails on any test-double TU (`Fake`, `fake`, `stub`, `mock` in the
        basename, or the test-local `tests/js_lifecycle_test.cpp` TU that
        defines `FakeJsRuntime`). Hermetic: stdlib file read only.
        """
        cmake_path = os.path.join(
            _REPO_ROOT, "engine", "runtime", "CMakeLists.txt"
        )
        try:
            with open(cmake_path, "r", encoding="utf-8") as handle:
                cmake_text = handle.read()
        except OSError as exc:
            self.fail(
                "check_prod_symbols: deny: CMakeLists unreadable (%s); "
                "failing closed" % exc
            )
        match = re.search(
            r"add_library\s*\(\s*runtime\b(.*?)\)",
            cmake_text,
            re.DOTALL,
        )
        self.assertIsNotNone(
            match, "runtime static lib source list not found; failing closed"
        )
        sources = re.findall(r"src/\S+\.cpp", match.group(1))
        self.assertTrue(sources, "no prod sources parsed; failing closed")
        bad = [
            src
            for src in sources
            if re.search(r"fake|stub|mock", os.path.basename(src), re.I)
            or "js_lifecycle_test" in src
        ]
        self.assertEqual(
            bad,
            [],
            "Fake/test-double TU linked into prod `runtime` lib:\n%s"
            % "\n".join(bad),
        )


def _dirty_tree_root():
    """Synthetic tree carrying every acceptance symbol (T1 fixture)."""
    import tempfile

    dirty = {
        "engine/runtime/src/js_runtime.cpp": (
            "class FakeJsRuntime {};\nFakeJsRuntime* make() { return nullptr; }\n"
            "bool createFake() { return false; }\n"
        ),
        "engine/runtime/include/runtime/js_runtime.h": (
            "struct FakeEngineHooks {};\nvoid createFake();\n"
        ),
        "engine/runtime/src/js_engine.cpp": (
            "struct FakeEngineHooks {};\nvoid createFake();\n"
        ),
        "engine/runtime/include/runtime/egl_blit.h": (
            "inline bool StubCreateSurface() { return true; }\n"
            "inline bool StubMakeCurrent() { return true; }\n"
            "inline bool StubSwapOnce() { return true; }\n"
        ),
        "engine/graphics/skia/present_skia.cpp": (
            "bool StubSkiaInit() { return true; }\n"
            "bool StubSkiaMakeContext() { return true; }\n"
            "bool StubSkiaSwapOnce() { return true; }\n"
        ),
        "engine/runtime/include/runtime/vulkan_baseline.h": (
            "// Fake device capabilities\n"
        ),
        "engine/text/src/shaper.cpp": "int FallbackAdvance() { return 0; }\n",
        "engine/text/include/muse/text/font_discovery.h": "// stubbed data\n",
        "engine/text/src/font_discovery.cpp": "// stubbed enumerator\n",
    }
    tmp = tempfile.mkdtemp(prefix="prod-symbols-dirty-")
    for relpath, text in dirty.items():
        path = os.path.join(tmp, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    return tmp


def _empty_tree_root():
    import tempfile

    tmp = tempfile.mkdtemp(prefix="prod-symbols-clean-")
    for relpath in _PROD_SYMBOL_TARGETS:
        path = os.path.join(tmp, relpath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("// clean production target: no doubles\n")
    return tmp


if __name__ == "__main__":
    unittest.main()
