"""Batch 4 (T8) — CI perf gate reading the manifest and naming the subsystem.

Covers NFR-PERFORMANCE-006 (AC-PERFORMANCE-012) with required deps
NFR-PERFORMANCE-002 (AC-PERFORMANCE-002) and NFR-TESTING-007
(TEST-PERF-GATE: AC-TESTING-027..031, read-only input beyond AC-012):

- exceeding any budget fails the build naming the subsystem;
- thresholds are imported from ``benchmark/budgets.json`` (single edit
  point, AC-TESTING-028);
- repeat-run noise-band/flake policy (AC-TESTING-029): a run is only a
  failure when the regression exceeds the noise band across repeated
  runs, otherwise it is tolerated/retried, not failed;
- injected regression detected, stable build stays in noise band
  (AC-TESTING-031 behavior).

Public boundary under test (plan Checklist T8)::

    gate(manifest_path: str, results: dict) -> GateResult(
        passed: bool,
        failures: list[Failure(subsystem: str, area: str,
                               budget: float, actual: float)])

``gate`` lives at ``benchmark.gate.run``; threshold loading lives at
``benchmark.gate.thresholds`` and reads the manifest named by
``manifest_path`` (``benchmark/budgets.json`` in production).

Hermetic: stdlib unittest only, tmp manifest/results files, in-memory
result dicts, one hermetic CLI subprocess. No network/secrets/services,
no device, no thermal control.

Expected state before implementation: RED — ``benchmark/gate/run.py``
and ``benchmark/gate/thresholds.py`` do not exist, so every test fails
(ModuleNotFoundError / missing-file assertions).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE_RUN = os.path.join(REPO_ROOT, "benchmark", "gate", "run.py")
GATE_THRESHOLDS = os.path.join(REPO_ROOT, "benchmark", "gate", "thresholds.py")

APP = "tapp"


def write_manifest(rows, reference_apps=None):
    """Write a tmp budgets.json-schema manifest; return its path.

    Each row: (area, subsystem, threshold). Scalar numeric thresholds
    keep the tmp fixtures minimal; see ASSUMPTIONs below.
    """
    doc = {
        "version": 1,
        "reference_apps": reference_apps or [APP],
        "budgets": [
            {
                "area": area,
                "subsystem": subsystem,
                "threshold": threshold,
                "unit": "ms",
                "reference_app": APP,
            }
            for area, subsystem, threshold in rows
        ],
    }
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(doc, tmp)
    tmp.close()
    return tmp.name


def import_gate():
    """Import the gate through its public boundary."""
    from benchmark.gate.run import Failure, GateResult, gate
    return Failure, GateResult, gate


def cleanup(*paths):
    for path in paths:
        try:
            os.unlink(path)
        except OSError:
            pass


class GateModuleTest(unittest.TestCase):
    def test_gate_run_module_exists(self):
        # Files: benchmark/gate/run.py per plan Checklist T8.
        self.assertTrue(os.path.isfile(GATE_RUN))

    def test_thresholds_module_exists(self):
        # Files: benchmark/gate/thresholds.py per plan Checklist T8.
        self.assertTrue(os.path.isfile(GATE_THRESHOLDS))


class GateVerdictTest(unittest.TestCase):
    def test_all_within_budget_passes(self):
        # AC-012/AC-002: nothing over budget -> build passes.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 60.0})
        finally:
            cleanup(manifest)
        self.assertTrue(result.passed)
        self.assertEqual(list(result.failures), [])

    def test_gate_result_shape(self):
        # GateResult exposes .passed (bool) and .failures (list).
        _, GateResult, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 60.0})
        finally:
            cleanup(manifest)
        self.assertIsInstance(result, GateResult)
        self.assertIsInstance(result.passed, bool)
        self.assertIsInstance(result.failures, list)

    def test_breach_fails_naming_subsystem(self):
        # AC-012 + AC-002: exceeding any budget fails naming the subsystem.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 160.0})
        finally:
            cleanup(manifest)
        self.assertFalse(result.passed)
        subsystems = [f.subsystem for f in result.failures]
        self.assertIn("lifecycle", subsystems)

    def test_failure_carries_subsystem_area_budget_actual(self):
        # Failure(subsystem, area, budget, actual) per Interface line.
        Failure, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 160.0})
        finally:
            cleanup(manifest)
        self.assertTrue(result.failures)
        failure = result.failures[0]
        self.assertIsInstance(failure, Failure)
        self.assertEqual(failure.subsystem, "lifecycle")
        self.assertEqual(failure.area, "startup")
        self.assertEqual(failure.budget, 80.0)
        self.assertEqual(failure.actual, 160.0)

    def test_multiple_breaches_all_named(self):
        # Every breaching subsystem is named, not just the first.
        _, _, gate = import_gate()
        manifest = write_manifest(
            [
                ("startup", "lifecycle", 80.0),
                ("frame", "graphics", 4.0),
            ]
        )
        try:
            result = gate(manifest, {"startup": 160.0, "frame": 8.0})
        finally:
            cleanup(manifest)
        self.assertFalse(result.passed)
        subsystems = {f.subsystem for f in result.failures}
        self.assertIn("lifecycle", subsystems)
        self.assertIn("graphics", subsystems)

    def test_actual_equal_to_budget_passes(self):
        # Boundary: actual == budget is not an excess.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 80.0})
        finally:
            cleanup(manifest)
        self.assertTrue(result.passed)


class GateNoiseBandTest(unittest.TestCase):
    def test_small_jitter_within_noise_band_passes(self):
        # AC-031: a stable build measured repeatedly stays inside the
        # noise band — a ~1% wobble over budget is not a failure.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 80.8})
        finally:
            cleanup(manifest)
        self.assertTrue(result.passed)

    def test_injected_regression_detected(self):
        # AC-031: an injected artificial regression (extra fullscreen
        # pass ~ doubling frame cost) is detected by the gate.
        _, _, gate = import_gate()
        manifest = write_manifest([("frame", "graphics", 4.0)])
        try:
            result = gate(manifest, {"frame": 8.0})
        finally:
            cleanup(manifest)
        self.assertFalse(result.passed)
        self.assertIn("graphics", [f.subsystem for f in result.failures])

    def test_stable_repeat_runs_stay_in_band(self):
        # AC-031: the same build measured repeatedly (in-band values)
        # stays green.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": [79.5, 80.2, 79.8]})
        finally:
            cleanup(manifest)
        self.assertTrue(result.passed)

    def test_single_spike_not_failed(self):
        # AC-029 flake policy: one spiky repeat among in-band runs is
        # retried/tolerated, not reported as a failure.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": [79.5, 160.0, 79.8]})
        finally:
            cleanup(manifest)
        self.assertTrue(result.passed)

    def test_consistent_excess_across_repeats_fails(self):
        # AC-029: a regression exceeding the noise band across repeated
        # runs is a failure naming the subsystem.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": [150.0, 155.0, 148.0]})
        finally:
            cleanup(manifest)
        self.assertFalse(result.passed)
        self.assertIn("lifecycle", [f.subsystem for f in result.failures])


class GateThresholdSourceTest(unittest.TestCase):
    def test_thresholds_single_edit_point(self):
        # AC-028: thresholds imported from the manifest — the same
        # results flip verdict when only the manifest edit changes.
        _, _, gate = import_gate()
        strict = write_manifest([("startup", "lifecycle", 80.0)])
        relaxed = write_manifest([("startup", "lifecycle", 200.0)])
        try:
            self.assertFalse(gate(strict, {"startup": 160.0}).passed)
            self.assertTrue(gate(relaxed, {"startup": 160.0}).passed)
        finally:
            cleanup(strict, relaxed)

    def test_thresholds_loader_reads_budgets_json(self):
        # benchmark/gate/thresholds.py loads thresholds from the
        # manifest file (no duplicated budget table in code).
        from benchmark.gate import thresholds as thresholds_mod
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            loaded = thresholds_mod.load_thresholds(manifest)
        finally:
            cleanup(manifest)
        self.assertIn("startup", loaded)


class GateInputValidationTest(unittest.TestCase):
    def test_missing_manifest_raises(self):
        # Fail closed: no manifest to read thresholds from.
        _, _, gate = import_gate()
        with self.assertRaises(Exception):
            gate(os.path.join(REPO_ROOT, "does-not-exist.json"), {"startup": 1.0})

    def test_malformed_manifest_raises(self):
        # Fail closed: unparseable manifest never silently passes.
        _, _, gate = import_gate()
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{not valid json")
        tmp.close()
        try:
            with self.assertRaises(Exception):
                gate(tmp.name, {"startup": 1.0})
        finally:
            cleanup(tmp.name)

    def test_unknown_area_does_not_crash(self):
        # Boundary: results naming an area absent from the manifest
        # must not crash the gate.
        _, _, gate = import_gate()
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            result = gate(manifest, {"startup": 60.0, "nope": 999.0})
        finally:
            cleanup(manifest)
        self.assertTrue(result.passed)


class GateCliTest(unittest.TestCase):
    def run_cli(self, manifest, results):
        results_path = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        json.dump(results, results_path)
        results_path.close()
        try:
            proc = subprocess.run(
                [sys.executable, GATE_RUN, manifest, results_path.name],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=60,
            )
        finally:
            cleanup(results_path.name)
        return proc

    def test_cli_breach_exit_nonzero_naming_subsystem(self):
        # Interface: exit nonzero naming subsystem on breach (AC-012).
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            proc = self.run_cli(manifest, {"startup": 160.0})
        finally:
            cleanup(manifest)
        self.assertNotEqual(proc.returncode, 0)
        self.assertIn("lifecycle", proc.stdout + proc.stderr)

    def test_cli_pass_exit_zero(self):
        manifest = write_manifest([("startup", "lifecycle", 80.0)])
        try:
            proc = self.run_cli(manifest, {"startup": 60.0})
        finally:
            cleanup(manifest)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
