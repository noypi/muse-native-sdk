"""Batch 2 (T4) — METHOD.md raw-data publication + independent-reproduction
section; create `benchmark/datasets` (AC-015, AC-016; AC-013 verified).

Covers NFR-PERFORMANCE-007 AC-013/015/016 / TC-013/015/016:

- (a) six-field claim template satisfies AC-013/TC-013;
- (b) Raw-data section: Perfetto/FrameTimeline + harness scripts
  published alongside results, dataset layout + `schema.json`,
  excluded samples retained with flags (AC-015);
- (c) Independent-reproduction section: clone -> documented commands on
  the listed device -> compare within stated tolerance (AC-016;
  external lab run itself is out of scope per A2).
- `benchmark/datasets/` exists with README + schema (TC-015 file check).

Public boundary under test (plan Checklist T4):

    none — doc + data directory (no callable surface; verified by
    TC-013/015/016 checks). Reproduction command surface is the
    documented CLI: `python3 benchmark/gate/run.py benchmark/budgets.json
    <results-json>` plus harness run commands, with stated tolerance
    (gate noise band + per-metric tolerance).

Hermetic: stdlib unittest only, reads committed text files and the
`benchmark/datasets` directory. No network/secrets/services/devices.

Expected state before implementation: RED — METHOD.md has no raw-data
or independent-reproduction section and `benchmark/datasets/` does not
exist, so the new-section and file-existence assertions fail.
"""

import json
import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
METHOD_MD = os.path.join(REPO_ROOT, "benchmark", "METHOD.md")
DATASETS_DIR = os.path.join(REPO_ROOT, "benchmark", "datasets")
DATASETS_README = os.path.join(DATASETS_DIR, "README.md")
DATASETS_SCHEMA = os.path.join(DATASETS_DIR, "schema.json")

GATE_CLI = "python3 benchmark/gate/run.py benchmark/budgets.json"

SIX_FIELDS = (
    "reference app",
    "device",
    "build type",
    "thermal state",
    "sample size",
    "raw-dataset link",
)


def read_method():
    with open(METHOD_MD, encoding="utf-8") as fh:
        return fh.read()


class SixFieldTemplateTest(unittest.TestCase):
    def test_six_attribution_fields_present(self):
        # AC-013/TC-013: reference app, device, build type, thermal
        # state, sample size, raw-dataset link.
        text = read_method().lower().replace("raw dataset", "raw-dataset")
        for field in SIX_FIELDS:
            self.assertIn(field, text, msg="missing attribution field: %s" % field)


class RawDataSectionTest(unittest.TestCase):
    def test_raw_data_section_exists(self):
        text = read_method().lower()
        self.assertTrue(
            "raw-data" in text or "raw data" in text,
            msg="METHOD.md must have a raw-data section",
        )

    def test_perfetto_frametimeline_mentioned(self):
        # AC-015: Perfetto/FrameTimeline traces published alongside results.
        text = read_method().lower()
        self.assertTrue(
            "perfetto" in text or "frametimeline" in text,
            msg="raw-data section must name Perfetto/FrameTimeline",
        )

    def test_harness_scripts_published(self):
        # AC-015: harness scripts published alongside results.
        text = read_method().lower()
        self.assertIn("harness", text)

    def test_dataset_layout_and_schema_referenced(self):
        # AC-015: dataset layout + schema.json.
        text = read_method().lower()
        self.assertIn("benchmark/datasets", text)
        self.assertIn("schema.json", text)

    def test_excluded_samples_retained_with_flags(self):
        # AC-015: excluded samples retained with flags.
        text = read_method().lower()
        self.assertIn("exclud", text)
        self.assertIn("flag", text)


class DatasetsDirectoryTest(unittest.TestCase):
    def test_datasets_dir_exists(self):
        # TC-015 file check target.
        self.assertTrue(os.path.isdir(DATASETS_DIR))

    def test_datasets_readme_exists(self):
        self.assertTrue(os.path.isfile(DATASETS_README))

    def test_datasets_schema_exists_and_is_valid_json(self):
        self.assertTrue(os.path.isfile(DATASETS_SCHEMA))
        with open(DATASETS_SCHEMA, encoding="utf-8") as fh:
            schema = json.load(fh)
        self.assertIsInstance(schema, dict)

    def test_datasets_schema_covers_exclusion_flags(self):
        # Schema must model the retained-with-flags rule: per-sample
        # exclusion flagging.
        with open(DATASETS_SCHEMA, encoding="utf-8") as fh:
            raw = fh.read().lower()
        self.assertIn("exclud", raw)


class IndependentReproductionTest(unittest.TestCase):
    def test_reproduction_section_exists(self):
        # AC-016: independent-reproduction section.
        text = read_method().lower()
        self.assertTrue(
            "reproduc" in text or "independent" in text,
            msg="METHOD.md must document independent reproduction",
        )

    def test_gate_cli_command_documented(self):
        # Reproduction command surface: gate CLI.
        text = read_method()
        self.assertIn("benchmark/gate/run.py", text)
        self.assertIn("benchmark/budgets.json", text)

    def test_tolerance_stated(self):
        # AC-016: compare within stated tolerance (gate noise band +
        # per-metric tolerance).
        text = read_method().lower()
        self.assertIn("tolerance", text)

    def test_noise_band_tolerance_referenced(self):
        # Tolerance ties to the gate noise band (T2 semantics).
        text = read_method().lower()
        self.assertTrue(
            "noise" in text,
            msg="tolerance must reference the gate noise band",
        )


if __name__ == "__main__":
    unittest.main()
