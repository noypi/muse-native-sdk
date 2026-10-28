"""Batch 2 (T3) — METHOD.md confidence level + outlier/thermal-drift exclusion rule.

Covers NFR-PERFORMANCE-007 AC-014 / TC-014 (manual inspection), depending
on T2 aggregation/noise-band semantics (median + 5% noise band):

- exclusion rule declares count + reason, `exclusions: none` default,
  excluded samples retained with flags;
- minimum repeat count / stated confidence level per claim;
- median + percentile aggregation consistent with the T2 gate;
- thermal-drift handling: as-measured state recorded, drift runs
  flagged/excluded only by the documented rule;
- Q-PERFORMANCE-001 device matrix is NOT invented (TBD marker stays).

Public boundary under test (plan Checklist T3):

    none — normative doc section (no callable surface; verified by
    TC-014 manual inspection).

Hermetic: stdlib unittest only, reads the committed
``benchmark/METHOD.md`` text. No network/secrets/services/devices.

Expected state before implementation: RED — the current METHOD.md has
the honest exclusion stub but no repeat/confidence section, no
median+percentile aggregation rule, and no thermal-drift handling, so
the new-section assertions fail.
"""

import os
import re
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
METHOD_MD = os.path.join(REPO_ROOT, "benchmark", "METHOD.md")


def read_method():
    with open(METHOD_MD, encoding="utf-8") as fh:
        return fh.read()


class MethodFileExistsTest(unittest.TestCase):
    def test_method_md_exists(self):
        self.assertTrue(os.path.isfile(METHOD_MD))


class ExclusionRuleTest(unittest.TestCase):
    def test_exclusion_declares_count_and_reason(self):
        # Pre-existing honest rule: excluded samples declared with count
        # and reason.
        text = read_method()
        self.assertIn("count and reason", text.lower())

    def test_exclusions_none_default(self):
        # Pre-existing default: state `exclusions: none` when nothing
        # was dropped.
        text = read_method()
        self.assertIn("exclusions: none", text.lower())

    def test_excluded_samples_retained_with_flags(self):
        # AC-014/AC-015: raw dataset retains excluded samples with flags.
        text = read_method().lower()
        self.assertIn("retain", text)
        self.assertIn("flag", text)


class ConfidenceSectionTest(unittest.TestCase):
    def test_minimum_repeat_count_stated(self):
        # AC-014: minimum repeat count per claim.
        text = read_method().lower()
        self.assertTrue(
            "repeat" in text and "minimum" in text,
            msg="METHOD.md must state a minimum repeat count per claim",
        )

    def test_confidence_level_stated(self):
        # AC-014: stated confidence level per claim.
        text = read_method().lower()
        self.assertIn("confidence", text)

    def test_median_plus_percentile_aggregation(self):
        # Consistent with T2's gate (median aggregation): median +
        # percentile aggregation rule.
        text = read_method().lower()
        self.assertIn("median", text)
        self.assertTrue(
            "percentile" in text or "p99" in text,
            msg="METHOD.md must document percentile (e.g. p99) aggregation",
        )


class ThermalDriftTest(unittest.TestCase):
    def test_thermal_drift_handling_documented(self):
        # AC-014: thermal-drift handling — as-measured state recorded,
        # drift runs flagged/excluded only by the documented rule.
        text = read_method().lower()
        self.assertTrue(
            "drift" in text,
            msg="METHOD.md must document thermal-drift handling",
        )

    def test_drift_exclusion_only_by_documented_rule(self):
        # Runs showing drift are flagged/excluded only via the exclusion
        # rule, not silently.
        text = read_method().lower()
        self.assertTrue(
            "drift" in text and "exclu" in text,
            msg="thermal drift must tie into the documented exclusion rule",
        )


class NoInventedMatrixTest(unittest.TestCase):
    def test_device_matrix_stays_tbd(self):
        # A1: Q-PERFORMANCE-001 unsettled — no invented device matrix.
        text = read_method()
        self.assertIn("TBD", text)
        self.assertNotRegex(
            text,
            re.compile(r"Pixel \d.*Galaxy S\d.*pass/fail", re.DOTALL),
            msg="METHOD.md must not invent a concrete device pass/fail matrix",
        )


if __name__ == "__main__":
    unittest.main()
