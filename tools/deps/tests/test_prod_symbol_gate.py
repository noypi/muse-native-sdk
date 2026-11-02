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

RED now: the four support.md leak paths are still present, so the
live-tree cases fail naming the offending symbols. They turn green when
T3/T4/T6/T8 move the doubles out of prod TUs (the coder then wires the
`nm`-based `tools/deps/prod_symbol_check.py` gate with the plan
Interface; this file is reconciled to import it per GR-12).

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
            if re.search(pattern, text):
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
        violations = scan_prod_symbols()
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
