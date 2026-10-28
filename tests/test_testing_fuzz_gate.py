"""Batch 1 (T1+T2) — fuzz target inventory + merge-time fuzz gate (FR-TESTING-003).

Covers FR-TESTING-003 / AC-TESTING-023 + AC-TESTING-026 / TC-TESTING-023
(``./gradlew :testing:fuzzTargetInventory``) + TC-TESTING-026
(``./gradlew :testing:fuzzGate``):

- T1: ``list_targets() -> list[dict]`` (each
  ``{name: str, family: str, parser: str, sanitizer: str}``);
  ``get_target(name: str) -> dict``;
  ``run_target(name: str, data: bytes, budget_ms: int) -> dict``
  returns ``{crash: bool, crash_id: str, signal: str}``.
  Registry holds >=1 target per AC-TESTING-023 family; ``run_target``
  executes the family's byte-level parser entry point under a bounded
  budget with deterministic crash ids (hash of family+bytes).
- T2: ``fuzz_gate(budget_seconds: int) -> dict`` returns
  ``{passed: bool, targets_run: int, new_crashes: list[str],
  seconds: float}``; gate fails (``passed`` False) iff
  ``new_crashes`` non-empty. One checked-in seed file per target;
  gate runs each target over its seeds plus bounded mutations within
  ``budget_seconds``; zero new crashes permitted.

Public boundaries under test: ``testing.fuzz.targets.list_targets``,
``testing.fuzz.targets.get_target``,
``testing.fuzz.targets.run_target``,
``testing.fuzz.gate.fuzz_gate``, plus
``testing/fuzz/corpus/manifest.json`` and the
``:testing:fuzzTargetInventory`` / ``:testing:fuzzGate`` cases in
``gradlew``.

Hermetic: stdlib unittest only (plus ``unittest.mock`` fakes at the
target boundary), tmp dirs, no network/downloads/device/secrets, no
wall-clock or socket reads in the tests (durations measured only as
upper bounds against the stated budgets).

Expected state before implementation: RED — ``testing/fuzz`` does
not exist, so every test errors (ModuleNotFoundError) or fails the
file-existence checks.
"""

import json
import os
import time
import unittest
from unittest import mock

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FUZZ_DIR = os.path.join(REPO_ROOT, "testing", "fuzz")
FUZZ_INIT = os.path.join(FUZZ_DIR, "__init__.py")
TARGETS_PY = os.path.join(FUZZ_DIR, "targets.py")
GATE_PY = os.path.join(FUZZ_DIR, "gate.py")
CORPUS_MANIFEST = os.path.join(FUZZ_DIR, "corpus", "manifest.json")
CORPUS_DIR = os.path.join(FUZZ_DIR, "corpus")

# AC-TESTING-023 codec targets: the spec groups
# FlatBuffers/protobuf/CBOR/MessagePack as one serialization family
# with 4 codec targets; every other family has >=1 target.
# (ASSUMPTION: each codec target's ``family`` value is its codec name.)
EXPECTED_FAMILIES = (
    "css-sass",
    "layout-solver",
    "text-shaping",
    "deeplink-uri",
    "flatbuffers",
    "protobuf",
    "cbor",
    "msgpack",
    "image-decoders",
    "ota-manifest",
)

EXPECTED_SANITIZERS = ("asan", "ubsan", "msan")


def _load_targets():
    from testing.fuzz.targets import list_targets

    return list_targets()


class TestFuzzFilesExist(unittest.TestCase):
    def test_fuzz_package_init_exists(self):
        self.assertTrue(
            os.path.isfile(FUZZ_INIT), "testing/fuzz/__init__.py missing (T1)"
        )

    def test_targets_module_exists(self):
        self.assertTrue(
            os.path.isfile(TARGETS_PY), "testing/fuzz/targets.py missing (T1)"
        )

    def test_gate_module_exists(self):
        self.assertTrue(
            os.path.isfile(GATE_PY), "testing/fuzz/gate.py missing (T2)"
        )

    def test_corpus_manifest_exists_and_is_json(self):
        self.assertTrue(
            os.path.isfile(CORPUS_MANIFEST),
            "testing/fuzz/corpus/manifest.json missing (T1/T2)",
        )
        with open(CORPUS_MANIFEST, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
        self.assertIsInstance(manifest, (dict, list))


class TestTargetInventory(unittest.TestCase):
    def test_list_targets_returns_list_of_dicts(self):
        targets = _load_targets()
        self.assertIsInstance(targets, list)
        self.assertGreaterEqual(len(targets), 8)
        for entry in targets:
            self.assertIsInstance(entry, dict)

    def test_every_target_has_required_keys_and_types(self):
        for entry in _load_targets():
            for key in ("name", "family", "parser", "sanitizer"):
                self.assertIn(key, entry)
                self.assertIsInstance(entry[key], str)
                self.assertTrue(entry[key], key + " must be non-empty")

    def test_every_ac023_family_has_a_target(self):
        families = {entry["family"] for entry in _load_targets()}
        for family in EXPECTED_FAMILIES:
            self.assertIn(
                family, families, "AC-TESTING-023 family %r has no target" % family
            )

    def test_target_names_are_unique(self):
        names = [entry["name"] for entry in _load_targets()]
        self.assertEqual(len(names), len(set(names)))

    def test_sanitizer_values_are_known_variants(self):
        for entry in _load_targets():
            self.assertIn(entry["sanitizer"], EXPECTED_SANITIZERS)

    def test_get_target_returns_matching_entry(self):
        from testing.fuzz.targets import get_target

        targets = _load_targets()
        for entry in targets:
            fetched = get_target(entry["name"])
            self.assertEqual(fetched, entry)

    def test_get_target_unknown_name_raises_key_error(self):
        from testing.fuzz.targets import get_target

        with self.assertRaises(KeyError):
            get_target("no-such-fuzzer-target")

    def test_get_target_unknown_family_name_fails_inventory(self):
        # Acceptance: an unknown family name must not resolve to a target.
        from testing.fuzz.targets import get_target

        with self.assertRaises((KeyError, ValueError)):
            get_target("css-sass-typo-family-target")


class TestRunTarget(unittest.TestCase):
    def test_run_target_returns_crash_shape(self):
        from testing.fuzz.targets import run_target

        name = _load_targets()[0]["name"]
        result = run_target(name, b"hello seed", 100)
        self.assertIsInstance(result, dict)
        for key in ("crash", "crash_id", "signal"):
            self.assertIn(key, result)
        self.assertIsInstance(result["crash"], bool)
        self.assertIsInstance(result["crash_id"], str)
        self.assertIsInstance(result["signal"], str)

    def test_run_target_crash_ids_are_deterministic(self):
        from testing.fuzz.targets import run_target

        name = _load_targets()[0]["name"]
        first = run_target(name, b"\x00\x01deterministic", 100)
        second = run_target(name, b"\x00\x01deterministic", 100)
        self.assertEqual(first["crash_id"], second["crash_id"])
        self.assertEqual(first["crash"], second["crash"])

    def test_run_target_distinct_bytes_distinct_ids(self):
        # Deterministic ids derive from family+bytes (hash), so distinct
        # inputs must not collapse to one id.
        from testing.fuzz.targets import run_target

        name = _load_targets()[0]["name"]
        first = run_target(name, b"seed-A", 100)
        second = run_target(name, b"seed-B", 100)
        self.assertNotEqual(first["crash_id"], second["crash_id"])

    def test_run_target_empty_input_does_not_throw(self):
        from testing.fuzz.targets import run_target

        name = _load_targets()[0]["name"]
        result = run_target(name, b"", 100)
        self.assertIn("crash", result)

    def test_run_target_unknown_name_raises_key_error(self):
        from testing.fuzz.targets import run_target

        with self.assertRaises(KeyError):
            run_target("no-such-fuzzer-target", b"data", 100)

    def test_run_target_respects_bounded_budget(self):
        from testing.fuzz.targets import run_target

        name = _load_targets()[0]["name"]
        started = time.monotonic()
        run_target(name, os.urandom(256), 50)
        elapsed_ms = (time.monotonic() - started) * 1000.0
        # Generous upper bound: budget 50ms must not run for seconds.
        self.assertLess(elapsed_ms, 5000)


class TestSeedCorpus(unittest.TestCase):
    def _manifest_seed_names(self):
        with open(CORPUS_MANIFEST, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
        if isinstance(manifest, dict):
            seeds = manifest.get("seeds", manifest)
            if isinstance(seeds, dict):
                return seeds
            names = []
            for row in seeds if isinstance(seeds, list) else []:
                if isinstance(row, dict):
                    names.append(row.get("target", row.get("name", "")))
                else:
                    names.append(row)
            return names
        names = []
        for row in manifest:
            if isinstance(row, dict):
                names.append(row.get("target", row.get("name", "")))
            else:
                names.append(row)
        return names

    def test_manifest_lists_seed_per_target(self):
        names = set(self._manifest_seed_names())
        for entry in _load_targets():
            self.assertIn(
                entry["name"],
                names,
                "target %r has no seed corpus entry" % entry["name"],
            )

    def test_corpus_has_eight_family_dirs_with_seeds(self):
        self.assertTrue(os.path.isdir(CORPUS_DIR))
        subdirs = [
            name
            for name in os.listdir(CORPUS_DIR)
            if os.path.isdir(os.path.join(CORPUS_DIR, name))
        ]
        self.assertGreaterEqual(len(subdirs), 8)
        for name in subdirs:
            entries = os.listdir(os.path.join(CORPUS_DIR, name))
            seeds = [entry for entry in entries if "seed" in entry.lower()]
            self.assertTrue(
                seeds, "corpus dir %r holds no seed-* file" % name
            )

    def test_every_target_family_has_a_corpus_dir(self):
        subdirs = set(
            name
            for name in os.listdir(CORPUS_DIR)
            if os.path.isdir(os.path.join(CORPUS_DIR, name))
        )
        for entry in _load_targets():
            self.assertIn(
                entry["family"],
                subdirs,
                "family %r has no testing/fuzz/corpus/<family>/ dir"
                % entry["family"],
            )


class TestFuzzGate(unittest.TestCase):
    def test_fuzz_gate_returns_gate_shape(self):
        from testing.fuzz.gate import fuzz_gate

        result = fuzz_gate(1)
        self.assertIsInstance(result, dict)
        for key in ("passed", "targets_run", "new_crashes", "seconds"):
            self.assertIn(key, result)
        self.assertIsInstance(result["passed"], bool)
        self.assertIsInstance(result["targets_run"], int)
        self.assertIsInstance(result["new_crashes"], list)
        self.assertIsInstance(result["seconds"], float)

    def test_fuzz_gate_runs_every_target_with_zero_new_crashes(self):
        from testing.fuzz.gate import fuzz_gate

        result = fuzz_gate(2)
        self.assertEqual(result["targets_run"], len(_load_targets()))
        self.assertEqual(result["new_crashes"], [])
        self.assertTrue(result["passed"])

    def test_fuzz_gate_respects_budget(self):
        from testing.fuzz.gate import fuzz_gate

        budget = 2
        started = time.monotonic()
        result = fuzz_gate(budget)
        elapsed = time.monotonic() - started
        # Upper bound only: the gate must be bounded, never exact.
        self.assertLess(elapsed, budget + 30)
        self.assertLessEqual(result["seconds"], budget + 30)

    def test_fuzz_gate_fails_and_names_target_on_crashing_seed(self):
        # Injecting a crashing seed makes the gate fail naming the target.
        from testing.fuzz import gate as gate_module

        victim = _load_targets()[0]["name"]
        real_run_target = gate_module.run_target

        def crashing(name, data, budget_ms):
            if name == victim:
                return {
                    "crash": True,
                    "crash_id": "test-crash-" + name,
                    "signal": "SIGSEGV",
                }
            return real_run_target(name, data, budget_ms)

        with mock.patch.object(gate_module, "run_target", crashing):
            result = gate_module.fuzz_gate(1)
        self.assertFalse(result["passed"])
        self.assertNotEqual(result["new_crashes"], [])
        named = " ".join(result["new_crashes"])
        self.assertIn(victim, named)

    def test_fuzz_gate_passed_iff_no_new_crashes(self):
        from testing.fuzz import gate as gate_module

        with mock.patch.object(
            gate_module,
            "run_target",
            lambda name, data, budget_ms: {
                "crash": True,
                "crash_id": "forced-" + name,
                "signal": "SIGABRT",
            },
        ):
            failing = gate_module.fuzz_gate(1)
        self.assertFalse(failing["passed"])
        self.assertTrue(failing["new_crashes"])

    def test_fuzz_gate_malicious_bytes_do_not_escape_harness(self):
        # Oversized / hostile inputs are handled inside run_target and
        # must surface as data (crash=True/False), never raise.
        from testing.fuzz.targets import run_target

        hostile = b"\x00" * 65536 + b"../../etc/passwd ${IFS}"
        for entry in _load_targets():
            result = run_target(entry["name"], hostile, 50)
            self.assertIn("crash", result)


if __name__ == "__main__":
    unittest.main()
