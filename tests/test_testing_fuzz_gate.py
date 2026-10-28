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


REGRESS_PY = os.path.join(FUZZ_DIR, "regress.py")
CONTINUOUS_PY = os.path.join(FUZZ_DIR, "continuous.py")
REGRESSIONS_DIR = os.path.join(FUZZ_DIR, "regressions")

# Deterministic counter for synthetic crash ids (no wall-clock/random).
_PROMOTE_SEQ = [0]


def _next_crash_id(prefix):
    _PROMOTE_SEQ[0] += 1
    return "%s-%04d" % (prefix, _PROMOTE_SEQ[0])


def _family_target_name(family):
    for entry in _load_targets():
        if entry["family"] == family:
            return entry["name"]
    raise AssertionError("no target for family %r (T1 registry)" % family)


def _resolve_repo_path(path):
    if os.path.isabs(path):
        return path
    return os.path.normpath(os.path.join(REPO_ROOT, path))


def _cleanup_regression_file(regression_path):
    full = _resolve_repo_path(regression_path)
    try:
        if os.path.isfile(full):
            os.remove(full)
    except OSError:
        pass


def _manifest_texts():
    texts = []
    if not os.path.isdir(REGRESSIONS_DIR):
        return texts
    for entry in sorted(os.listdir(REGRESSIONS_DIR)):
        if entry.endswith(".json"):
            candidate = os.path.join(REGRESSIONS_DIR, entry)
            try:
                with open(candidate, "r", encoding="utf-8") as fh:
                    texts.append(fh.read())
            except OSError:
                pass
    return texts


class TestRegressFilesExist(unittest.TestCase):
    def test_regress_module_exists(self):
        self.assertTrue(
            os.path.isfile(REGRESS_PY), "testing/fuzz/regress.py missing (T3)"
        )

    def test_continuous_module_exists(self):
        self.assertTrue(
            os.path.isfile(CONTINUOUS_PY),
            "testing/fuzz/continuous.py missing (T4)",
        )


class TestPromoteCrash(unittest.TestCase):
    def test_promote_crash_returns_regression_shape(self):
        from testing.fuzz.regress import promote_crash

        family = "css-sass"
        crash_id = _next_crash_id("unittest-t3-shape")
        crash_bytes = b"FUZZ-CRASH:shape-probe"
        result = promote_crash(family, crash_bytes, crash_id)
        try:
            self.assertIsInstance(result, dict)
            for key in ("regression_path", "test_name"):
                self.assertIn(key, result)
                self.assertIsInstance(result[key], str)
                self.assertTrue(result[key], key + " must be non-empty")
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_promote_crash_writes_bytes_under_family_crash_id_bin(self):
        from testing.fuzz.regress import promote_crash

        family = "layout-solver"
        crash_id = _next_crash_id("unittest-t3-bytes")
        crash_bytes = b"FUZZ-CRASH:bytes-probe\x00\x01"
        result = promote_crash(family, crash_bytes, crash_id)
        try:
            full = _resolve_repo_path(result["regression_path"])
            self.assertTrue(
                os.path.isfile(full),
                "regression file missing at %r" % result["regression_path"],
            )
            with open(full, "rb") as fh:
                self.assertEqual(fh.read(), crash_bytes)
            self.assertIn(family, result["regression_path"])
            self.assertIn(crash_id, result["regression_path"])
            self.assertTrue(result["regression_path"].endswith(".bin"))
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_promote_crash_regression_path_lives_under_regressions_dir(self):
        # Plan Detail (quoted verbatim): "writes a checked-in regression
        # case under `testing/fuzz/regressions/<family>/<crash_id>.bin`".
        from testing.fuzz.regress import promote_crash

        family = "text-shaping"
        crash_id = _next_crash_id("unittest-t3-layout")
        result = promote_crash(family, b"FUZZ-CRASH:layout-probe", crash_id)
        try:
            full = _resolve_repo_path(result["regression_path"])
            self.assertTrue(
                full.startswith(REGRESSIONS_DIR + os.sep),
                "regression path escapes testing/fuzz/regressions: %r"
                % result["regression_path"],
            )
            expected_tail = os.path.join(family, crash_id + ".bin")
            self.assertTrue(
                result["regression_path"].replace("\\", "/").endswith(
                    expected_tail.replace("\\", "/")
                ),
                "regression path must end with %r, got %r"
                % (expected_tail, result["regression_path"]),
            )
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_promote_crash_is_idempotent(self):
        # Promoting the same crash twice yields the same result and a
        # single file (no duplicate manifest rows / files).
        from testing.fuzz.regress import promote_crash

        family = "deeplink-uri"
        crash_id = _next_crash_id("unittest-t3-idem")
        crash_bytes = b"FUZZ-CRASH:idempotent-probe"
        first = promote_crash(family, crash_bytes, crash_id)
        try:
            second = promote_crash(family, crash_bytes, crash_id)
            self.assertEqual(first, second)
            full = _resolve_repo_path(first["regression_path"])
            self.assertTrue(os.path.isfile(full))
            with open(full, "rb") as fh:
                self.assertEqual(fh.read(), crash_bytes)
            hits = sum(
                text.count(crash_id) for text in _manifest_texts()
            )
            # At most one manifest row per crash id (a manifest row is
            # required by the plan; duplicates are not).
            # (ASSUMPTION: manifest is a JSON file under
            # testing/fuzz/regressions/ mapping crash_id to fix-status.)
            self.assertLessEqual(hits, 1)
        finally:
            _cleanup_regression_file(first.get("regression_path", ""))

    def test_promote_crash_records_manifest_row_with_fix_status(self):
        # Plan Detail (quoted verbatim): "a manifest row mapping
        # crash_id→fix-status".
        # (ASSUMPTION: manifest is a JSON file under
        # testing/fuzz/regressions/ mapping crash_id to fix-status.)
        from testing.fuzz.regress import promote_crash

        family = "flatbuffers"
        crash_id = _next_crash_id("unittest-t3-manifest")
        result = promote_crash(family, b"FUZZ-CRASH:manifest-probe", crash_id)
        try:
            texts = _manifest_texts()
            self.assertTrue(
                texts, "no manifest JSON under testing/fuzz/regressions (T3)"
            )
            self.assertTrue(
                any(crash_id in text for text in texts),
                "manifest has no row for crash %r" % crash_id,
            )
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_promote_crash_rerun_reproduces_crash_id(self):
        # Plan Acceptance (quoted verbatim): "promote a synthetic crash,
        # assert the regression file exists and re-running `run_target`
        # on it reproduces the crash id until marked fixed."
        from testing.fuzz.regress import promote_crash
        from testing.fuzz.targets import run_target

        family = "cbor"
        target = _family_target_name(family)
        crash_bytes = b"FUZZ-CRASH:repro-probe-" + family.encode("utf-8")
        observed = run_target(target, crash_bytes, 100)
        crash_id = observed["crash_id"]
        result = promote_crash(family, crash_bytes, crash_id)
        try:
            full = _resolve_repo_path(result["regression_path"])
            with open(full, "rb") as fh:
                stored = fh.read()
            rerun = run_target(target, stored, 100)
            self.assertEqual(rerun["crash_id"], crash_id)
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_promote_crash_unknown_family_raises_value_error(self):
        # Plan Detail (quoted verbatim): "unknown family raises
        # `ValueError`."
        from testing.fuzz.regress import promote_crash

        with self.assertRaises(ValueError):
            promote_crash(
                "no-such-family",
                b"FUZZ-CRASH:unknown-family",
                _next_crash_id("unittest-t3-unknown"),
            )

    def test_list_regressions_returns_list_of_strings(self):
        from testing.fuzz.regress import list_regressions, promote_crash

        family = "msgpack"
        crash_id = _next_crash_id("unittest-t3-list")
        result = promote_crash(family, b"FUZZ-CRASH:list-probe", crash_id)
        try:
            rows = list_regressions(family)
            self.assertIsInstance(rows, list)
            for row in rows:
                self.assertIsInstance(row, str)
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_list_regressions_contains_promoted_crash(self):
        from testing.fuzz.regress import list_regressions, promote_crash

        family = "protobuf"
        crash_id = _next_crash_id("unittest-t3-contained")
        result = promote_crash(family, b"FUZZ-CRASH:contained-probe", crash_id)
        try:
            rows = list_regressions(family)
            self.assertTrue(
                any(crash_id in row for row in rows),
                "list_regressions(%r) omits promoted crash %r"
                % (family, crash_id),
            )
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_list_regressions_unknown_family_raises_value_error(self):
        from testing.fuzz.regress import list_regressions

        with self.assertRaises(ValueError):
            list_regressions("no-such-family")

    def test_promote_crash_empty_bytes_still_records_case(self):
        from testing.fuzz.regress import list_regressions, promote_crash

        family = "image-decoders"
        crash_id = _next_crash_id("unittest-t3-empty")
        result = promote_crash(family, b"", crash_id)
        try:
            full = _resolve_repo_path(result["regression_path"])
            self.assertTrue(os.path.isfile(full))
            rows = list_regressions(family)
            self.assertTrue(any(crash_id in row for row in rows))
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))

    def test_promote_crash_hostile_crash_id_does_not_escape_regressions(self):
        # Security: a path-traversal crash id must stay inside the
        # regressions tree (contained) or be rejected — never write
        # outside testing/fuzz/regressions.
        from testing.fuzz.regress import promote_crash

        hostile = "../../../tmp-evil-%s" % _next_crash_id("unittest-t3")
        try:
            result = promote_crash(
                "ota-manifest", b"FUZZ-CRASH:hostile-probe", hostile
            )
        except ValueError:
            return
        try:
            full = _resolve_repo_path(result["regression_path"])
            self.assertTrue(
                full.startswith(REGRESSIONS_DIR + os.sep),
                "hostile crash id escaped regressions dir: %r"
                % result["regression_path"],
            )
        finally:
            try:
                _cleanup_regression_file(result.get("regression_path", ""))
            except NameError:
                pass

    def test_promote_crash_oversized_bytes_do_not_raise(self):
        # Security: oversized inputs surface as a stored case, never an
        # unhandled exception.
        from testing.fuzz.regress import promote_crash

        family = "ota-manifest"
        crash_id = _next_crash_id("unittest-t3-huge")
        huge = b"\x00" * 65536 + b"FUZZ-CRASH:huge-probe"
        result = promote_crash(family, huge, crash_id)
        try:
            full = _resolve_repo_path(result["regression_path"])
            self.assertTrue(os.path.isfile(full))
        finally:
            _cleanup_regression_file(result.get("regression_path", ""))


class TestContinuousFuzz(unittest.TestCase):
    def test_schedule_entry_returns_entry_shape(self):
        from testing.fuzz.continuous import schedule_entry

        target = _load_targets()[0]["name"]
        entry = schedule_entry(target)
        self.assertIsInstance(entry, dict)
        for key in ("target", "sanitizer", "owner_alias", "corpus"):
            self.assertIn(key, entry)
            self.assertIsInstance(entry[key], str)
            self.assertTrue(entry[key], key + " must be non-empty")

    def test_schedule_entry_target_echoes_request(self):
        from testing.fuzz.continuous import schedule_entry

        for item in _load_targets():
            entry = schedule_entry(item["name"])
            self.assertEqual(entry["target"], item["name"])

    def test_schedule_entry_sanitizer_is_known_variant(self):
        from testing.fuzz.continuous import schedule_entry

        for item in _load_targets():
            entry = schedule_entry(item["name"])
            self.assertIn(entry["sanitizer"], EXPECTED_SANITIZERS)

    def test_every_target_has_a_sanitizer_schedule_entry(self):
        # Plan Acceptance (quoted verbatim): "all targets have a
        # sanitizer schedule entry".
        from testing.fuzz.continuous import schedule_entry

        for item in _load_targets():
            entry = schedule_entry(item["name"])
            self.assertTrue(entry["sanitizer"])
            self.assertTrue(entry["owner_alias"])
            self.assertTrue(entry["corpus"])

    def test_schedule_entry_unknown_target_raises(self):
        from testing.fuzz.continuous import schedule_entry

        with self.assertRaises((KeyError, ValueError)):
            schedule_entry("no-such-fuzzer-target")

    def test_file_finding_returns_finding_shape(self):
        from testing.fuzz.continuous import file_finding, schedule_entry

        item = _load_targets()[0]
        entry = schedule_entry(item["name"])
        crash_id = _next_crash_id("unittest-t4-finding")
        finding = file_finding(
            entry["owner_alias"], crash_id, item["family"]
        )
        try:
            self.assertIsInstance(finding, dict)
            for key in ("finding_id", "owner_alias", "regression_path"):
                self.assertIn(key, finding)
                self.assertIsInstance(finding[key], str)
                self.assertTrue(finding[key], key + " must be non-empty")
        finally:
            _cleanup_regression_file(finding.get("regression_path", ""))

    def test_file_finding_routes_to_owning_alias(self):
        # Plan Acceptance (quoted verbatim): "a synthetic finding files
        # against the owning alias and produces a regression path."
        from testing.fuzz.continuous import file_finding, schedule_entry

        item = _load_targets()[3]
        owner = schedule_entry(item["name"])["owner_alias"]
        crash_id = _next_crash_id("unittest-t4-owner")
        finding = file_finding(owner, crash_id, item["family"])
        try:
            self.assertEqual(finding["owner_alias"], owner)
            self.assertTrue(finding["regression_path"])
        finally:
            _cleanup_regression_file(finding.get("regression_path", ""))

    def test_file_finding_auto_creates_t3_regression_case(self):
        # Plan Detail (quoted verbatim): "`file_finding` routes to the
        # owning alias and auto-creates the T3 regression case."
        from testing.fuzz.continuous import file_finding, schedule_entry
        from testing.fuzz.regress import list_regressions

        item = _load_targets()[5]
        owner = schedule_entry(item["name"])["owner_alias"]
        crash_id = _next_crash_id("unittest-t4-autocreate")
        finding = file_finding(owner, crash_id, item["family"])
        try:
            full = _resolve_repo_path(finding["regression_path"])
            self.assertTrue(
                os.path.isfile(full),
                "finding produced no regression file at %r"
                % finding["regression_path"],
            )
            rows = list_regressions(item["family"])
            self.assertTrue(
                any(crash_id in row for row in rows),
                "auto-created case missing from list_regressions(%r)"
                % item["family"],
            )
        finally:
            _cleanup_regression_file(finding.get("regression_path", ""))

    def test_file_finding_regression_reproduces_crash_id(self):
        from testing.fuzz.continuous import file_finding, schedule_entry
        from testing.fuzz.targets import run_target

        item = _load_targets()[6]
        owner = schedule_entry(item["name"])["owner_alias"]
        crash_bytes = b"FUZZ-CRASH:t4-repro-" + item["family"].encode("utf-8")
        crash_id = run_target(item["name"], crash_bytes, 100)["crash_id"]
        finding = file_finding(owner, crash_id, item["family"])
        try:
            full = _resolve_repo_path(finding["regression_path"])
            with open(full, "rb") as fh:
                stored = fh.read()
            rerun = run_target(item["name"], stored, 100)
            self.assertEqual(rerun["crash_id"], crash_id)
        finally:
            _cleanup_regression_file(finding.get("regression_path", ""))

    def test_file_finding_unknown_family_raises_value_error(self):
        # (ASSUMPTION: an unknown family is rejected with ValueError,
        # mirroring T3's "unknown family raises `ValueError`".)
        from testing.fuzz.continuous import file_finding

        with self.assertRaises(ValueError):
            file_finding(
                "some-owner",
                _next_crash_id("unittest-t4-unknown"),
                "no-such-family",
            )

    def test_file_finding_hostile_crash_id_does_not_escape(self):
        # Security: a traversal crash id via findings must stay inside
        # the regressions tree or be rejected.
        from testing.fuzz.continuous import file_finding, schedule_entry

        item = _load_targets()[0]
        owner = schedule_entry(item["name"])["owner_alias"]
        hostile = "../../../tmp-evil-%s" % _next_crash_id("unittest-t4")
        try:
            finding = file_finding(owner, hostile, item["family"])
        except ValueError:
            return
        try:
            full = _resolve_repo_path(finding["regression_path"])
            self.assertTrue(
                full.startswith(REGRESSIONS_DIR + os.sep),
                "hostile crash id escaped regressions dir: %r"
                % finding["regression_path"],
            )
        finally:
            try:
                _cleanup_regression_file(finding.get("regression_path", ""))
            except NameError:
                pass

    def test_file_finding_empty_owner_alias_rejected(self):
        # Security: findings must file against a real owning alias —
        # an empty alias must not produce a routed finding.
        # (ASSUMPTION: empty owner_alias raises ValueError.)
        from testing.fuzz.continuous import file_finding

        with self.assertRaises(ValueError):
            file_finding(
                "", _next_crash_id("unittest-t4-empty-owner"), "css-sass"
            )


class TestGradlewFuzzWiring(unittest.TestCase):
    """Batch 4 (T8) — gradlew wiring for the fuzz suite (FR-TESTING-003).

    Covers FR-TESTING-003 / AC-TESTING-023..026 wiring:
    ``gradlew :testing:<task>`` tasks ``fuzzTargetInventory``,
    ``fuzzRegressionPromotion``, ``continuousFuzz``, ``fuzzGate``
    (exit 0 pass / non-zero fail). The per-task pass/fail shapes are
    covered by the T1–T4 unit tests above; these tests prove the
    ``gradlew`` case-statement dispatches each task following the
    existing ``:testing:*`` pattern.

    Hermetic: stdlib unittest + subprocess of the repo ``gradlew``
    shim only; no network/downloads/device/secrets.
    """

    TASKS = (
        "fuzzTargetInventory",
        "fuzzRegressionPromotion",
        "continuousFuzz",
        "fuzzGate",
    )
    GRADLEW_PATH = os.path.join(REPO_ROOT, "gradlew")

    def _gradlew_text(self):
        with open(self.GRADLEW_PATH, "r", encoding="utf-8") as handle:
            return handle.read()

    def test_gradlew_declares_fuzz_tasks(self):
        text = self._gradlew_text()
        for task in self.TASKS:
            self.assertIn(
                ":testing:%s" % task,
                text,
                "gradlew missing :testing:%s case" % task,
            )

    def test_gradlew_fuzz_tasks_exit_zero(self):
        import subprocess

        for task in self.TASKS:
            proc = subprocess.run(
                ["sh", self.GRADLEW_PATH, ":testing:%s" % task],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=180,
            )
            self.assertEqual(
                proc.returncode,
                0,
                ":testing:%s exited %s: %s"
                % (task, proc.returncode, proc.stderr[-2000:]),
            )

    def test_gradlew_unknown_fuzz_task_fails_nonzero(self):
        import subprocess

        proc = subprocess.run(
            ["sh", self.GRADLEW_PATH, ":testing:noSuchFuzzTaskXYZ"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
