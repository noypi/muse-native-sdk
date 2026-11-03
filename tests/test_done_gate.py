"""Batch 3 (T6) — done gate RED pins.

Covers FR-TESTING-007 / AC-TESTING-041 (coder NOT-DONE without build
success + prod-graph boot + one real effect log + ``git status``
clean; done requires all four) / TC-TESTING-041
(``python3 testing/gates/done_gate.py``).

Public boundary under test: ``testing.gates.done_gate`` with
``main(build_ok: bool, booted: bool, effect_logged: bool,
tree_clean: bool) -> int`` (0 done, nonzero NOT-DONE).

Hermetic: stdlib unittest only, no network/downloads/device/secrets,
no wall-clock reads, no live ``git status`` — conditions are passed
as plain bools.

Expected state before implementation: RED — ``main`` raises
NotImplementedError, so every behavior test fails for the expected
reason (``python3 -m unittest tests.test_done_gate``).
"""

import inspect
import sys
import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

try:
    from testing.gates import done_gate as gate
except ImportError:
    gate = None


class DoneGateInterfaceTest(unittest.TestCase):
    def test_module_and_main_exist(self):
        self.assertIsNotNone(gate, "T6 not implemented: testing/gates/done_gate.py missing")

    def test_main_signature_matches_plan(self):
        params = list(inspect.signature(gate.main).parameters)
        self.assertEqual(params, ["build_ok", "booted", "effect_logged", "tree_clean"])


class DoneGateBehaviorTest(unittest.TestCase):
    def test_done_all_four_hold_returns_zero(self):
        self.assertEqual(gate.main(True, True, True, True), 0)

    def test_no_build_success_is_not_done(self):
        self.assertNotEqual(gate.main(False, True, True, True), 0)

    def test_no_boot_is_not_done(self):
        self.assertNotEqual(gate.main(True, False, True, True), 0)

    def test_no_effect_log_is_not_done(self):
        self.assertNotEqual(gate.main(True, True, False, True), 0)

    def test_dirty_tree_is_not_done(self):
        self.assertNotEqual(gate.main(True, True, True, False), 0)

    def test_nothing_holds_is_not_done(self):
        self.assertNotEqual(gate.main(False, False, False, False), 0)


if __name__ == "__main__":
    unittest.main()
