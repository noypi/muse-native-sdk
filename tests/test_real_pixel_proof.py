"""Batch 3 (T7) — real pixel proof + runner wiring RED pins.

Covers FR-TESTING-007 / AC-TESTING-042 (``runRealPixelProof`` actually
counts ``isBlankViewport`` and diffs with ``diffAgainstGolden`` versus
committed goldens — never hardcoded ``0,0`` — and re-baseline is
refused while any proof row is red) / TC-TESTING-042
(``python3 testing/gates/real_pixel_proof.py``); runner wiring in
``testing/gates/run_gates.py``.

Public boundaries under test: ``testing.gates.real_pixel_proof``
with ``isBlankViewport``, ``diffAgainstGolden`` (spec-named,
AC-TESTING-042), and ``runRealPixelProof(viewport: Image)`` returning
``{ blankCount: int, diff: DiffResult }``; ``testing.gates.run_gates``
with ``main(argv: list[str]) -> int`` (repo gate precedent).

Hermetic: stdlib unittest only; viewports are in-memory nested tuples
of RGB ints (no images, no goldens checkout, no device); the
re-baseline refusal runs in a tmp dir via subprocess ``cwd`` (the
test process never chdirs); nothing is downloaded, no secrets or app
config are read, no live service is contacted.

Expected state before implementation: RED — every callable raises
NotImplementedError, so each behavior test fails for the expected
reason (``python3 -m unittest tests.test_real_pixel_proof``).
"""

import ast
import inspect
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

try:
    from testing.gates import real_pixel_proof as proof
except ImportError:
    proof = None

try:
    from testing.gates import run_gates as runner
except ImportError:
    runner = None

SCRIPT = os.path.join(REPO_ROOT, "testing", "gates", "real_pixel_proof.py")
RUNNER_SCRIPT = os.path.join(REPO_ROOT, "testing", "gates", "run_gates.py")

# Fake viewports: nested tuples of RGB ints. Uniform == blank candidate,
# varied == real pixels. Shape is an ASSUMPTION (see T7.md); the coder
# owns the real Image type.
BLANK_VIEWPORT = tuple(tuple((0, 0, 0) for _ in range(4)) for _ in range(4))
REAL_VIEWPORT = tuple(
    tuple(((r * 37 + c * 11) % 256, (r * 17 + c * 53) % 256, (r + c) % 256)
          for c in range(4))
    for r in range(4)
)


def read_source(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


class RealPixelProofInterfaceTest(unittest.TestCase):
    def test_module_exists(self):
        self.assertIsNotNone(proof, "T7 not implemented: testing/gates/real_pixel_proof.py missing")

    def test_spec_named_predicates_exist(self):
        self.assertTrue(callable(proof.isBlankViewport))
        self.assertTrue(callable(proof.diffAgainstGolden))
        self.assertTrue(callable(proof.runRealPixelProof))


class BlankCountingTest(unittest.TestCase):
    def test_blank_viewport_counts_one(self):
        result = proof.runRealPixelProof(BLANK_VIEWPORT)
        self.assertEqual(result.blankCount, 1)

    def test_real_viewport_counts_zero(self):
        result = proof.runRealPixelProof(REAL_VIEWPORT)
        self.assertEqual(result.blankCount, 0)

    def test_counts_vary_with_input_never_constant(self):
        # A hardcoded 0,0 implementation returns the same pair for any input.
        blank = proof.runRealPixelProof(BLANK_VIEWPORT)
        real = proof.runRealPixelProof(REAL_VIEWPORT)
        self.assertNotEqual((blank.blankCount, blank.diff),
                            (real.blankCount, real.diff))

    def test_result_carries_blank_count_and_diff(self):
        result = proof.runRealPixelProof(REAL_VIEWPORT)
        self.assertIsInstance(result.blankCount, int)
        self.assertIsNotNone(result.diff)


class NoHardcodedZeroTest(unittest.TestCase):
    def test_no_hardcoded_zero_pair_return(self):
        # Narrow static pin (behavioral tests above are primary): the
        # proof must never `return 0, 0` / `return (0, 0)`.
        source = read_source(SCRIPT)
        self.assertNotIn("return 0, 0", source)
        self.assertNotIn("return (0, 0)", source)

    def test_proof_routes_through_named_predicates(self):
        # AST pin: runRealPixelProof must actually call isBlankViewport
        # and diffAgainstGolden (AC-TESTING-042), not inline constants.
        tree = ast.parse(read_source(SCRIPT))
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef,)) and node.name == "runRealPixelProof":
                calls = {n.func.attr for n in ast.walk(node)
                         if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)}
                names = {n.func.id for n in ast.walk(node)
                         if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)}
                self.assertIn("isBlankViewport", names | calls)
                self.assertIn("diffAgainstGolden", names | calls)
                return
        self.fail("runRealPixelProof not found in real_pixel_proof.py")


class RebaselineRefusalTest(unittest.TestCase):
    # ASSUMPTION (see T7.md): runner entry is main(argv)->int per repo
    # gate precedent; argv flag spelling is the coder's, call sites
    # reconcile without changing expected behavior.
    def test_runner_module_exists(self):
        self.assertIsNotNone(runner, "T7 not implemented: testing/gates/run_gates.py missing")

    def test_red_proof_refuses_rebaseline_nonzero(self):
        self.assertNotEqual(runner.main(["--proof", "red"]), 0)

    def test_red_proof_writes_no_golden_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc = subprocess.run(
                [sys.executable, RUNNER_SCRIPT, "--proof", "red"],
                cwd=tmp, capture_output=True, text=True, timeout=60)
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(os.listdir(tmp), [])


if __name__ == "__main__":
    unittest.main()
