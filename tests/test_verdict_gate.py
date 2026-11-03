"""Batch 3 (T5) — verdict gate RED pins.

Covers FR-TESTING-007 / AC-TESTING-040 (tester PASS needs local exit 0
+ ``gh pr checks`` 0 pending 0 fail + every plan/pr-body box ``[x]``
with evidence; otherwise PARTIAL/FAIL) / TC-TESTING-040
(``python3 testing/gates/verdict_gate.py``).

Public boundary under test: ``testing.gates.verdict_gate`` with
``main(build_ok: bool, checks_pending: int, checks_failed: int,
boxes_checked: bool) -> int`` (0 PASS, nonzero PARTIAL/FAIL).

Hermetic: stdlib unittest only, no network/downloads/device/secrets,
no wall-clock reads, no live ``gh`` — counts are passed as plain ints.

Expected state before implementation: RED — ``main`` raises
NotImplementedError, so every behavior test fails for the expected
reason (``python3 -m unittest tests.test_verdict_gate``).
"""

import inspect
import sys
import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

try:
    from testing.gates import verdict_gate as gate
except ImportError:
    gate = None


class VerdictGateInterfaceTest(unittest.TestCase):
    def test_module_and_main_exist(self):
        self.assertIsNotNone(gate, "T5 not implemented: testing/gates/verdict_gate.py missing")

    def test_main_signature_matches_plan(self):
        params = list(inspect.signature(gate.main).parameters)
        self.assertEqual(params, ["build_ok", "checks_pending", "checks_failed", "boxes_checked"])


class VerdictGateBehaviorTest(unittest.TestCase):
    def test_pass_all_green_returns_zero(self):
        self.assertEqual(gate.main(True, 0, 0, True), 0)

    def test_build_failed_returns_nonzero(self):
        self.assertNotEqual(gate.main(False, 0, 0, True), 0)

    def test_pending_checks_return_nonzero(self):
        self.assertNotEqual(gate.main(True, 2, 0, True), 0)

    def test_failed_checks_return_nonzero(self):
        self.assertNotEqual(gate.main(True, 0, 1, True), 0)

    def test_unchecked_boxes_return_nonzero(self):
        self.assertNotEqual(gate.main(True, 0, 0, False), 0)

    def test_all_red_returns_nonzero(self):
        self.assertNotEqual(gate.main(False, 3, 2, False), 0)

    def test_negative_pending_is_rejected_nonzero(self):
        # ASSUMPTION (see T5.md): negative counts are a usage error, never PASS.
        self.assertNotEqual(gate.main(True, -1, 0, True), 0)

    def test_negative_failed_is_rejected_nonzero(self):
        # ASSUMPTION (see T5.md): negative counts are a usage error, never PASS.
        self.assertNotEqual(gate.main(True, 0, -1, True), 0)


if __name__ == "__main__":
    unittest.main()
