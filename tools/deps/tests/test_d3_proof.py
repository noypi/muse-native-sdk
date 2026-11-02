#!/usr/bin/env python3
"""Batch 4 (T7) hermetic proof tests for `vendor-d3`.

Covers plan.md Batch 4 "Proof" T7 (plan Interface:
`proof() -> {parity: exit 0, nm: D3 symbols present, production_link:
exit 0, seam: exit 0}`):
- parity expectations (hermetic C++17 half): the checked-in HarfBuzz
  reference table rows the parity test asserts, plus the source contract
  of `engine/text/tests/harfbuzz_parity_test.cpp` (stdlib-only includes,
  no device/JVM/network; ctest registration present).
- gate halves: `production_link_check`, `seam_check`, `admission_check`
  `--self-test` equivalents exit 0 (called in-process, hermetic).
- weak-probe presence: `engine/text/src/shaper.cpp` declares the
  `hb_shape` / `FT_Init_FreeType` / `u_getVersion` weak refs and
  null-checks all three (unit-testable half of the `nm` signal).
- `nm` delta on the real Android `.so` (device half): asserted when the
  ABI artifacts exist, otherwise fails naming the exact device command
  the coder/tester must run (never run here).

Per GR-08 T1.8 this file is assigned to T7 (first and only item using
it) and mapped in unit-tests/T7.md.

Hermetic: Python stdlib `unittest` + `os`/`sys` only, plus the
in-checkout `tools.deps` gate modules under test (imported, never
executed as subprocesses). No network, no secrets, no application
config, no live service. `nm`/artifact inspection is read-only; the
device/emulator builds themselves are EXTERNAL (owning layer:
tester/T7 device work).
"""

import os
import subprocess
import sys
import unittest

sys.path.insert(
    0,
    os.path.dirname(
        os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        )
    ),
)

from tools.deps import admission_check as _admission_check
from tools.deps import production_link_check as _production_link_check
from tools.deps import seam_check as _seam_check
from tools.deps._deps_data import load_manifest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_RUN_CHECKOUT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)

_MANIFEST = os.path.join(_RUN_CHECKOUT, "build", "dependency-manifest.json")
_PARITY_CPP = os.path.join(
    _RUN_CHECKOUT, "engine", "text", "tests",
    "harfbuzz_parity_test.cpp",
)
_PARITY_REF = os.path.join(
    _RUN_CHECKOUT, "engine", "text", "tests", "data",
    "harfbuzz_ref.tsv",
)
_TEXT_CMAKE = os.path.join(
    _RUN_CHECKOUT, "engine", "text", "CMakeLists.txt"
)
_SHAPER_CPP = os.path.join(
    _RUN_CHECKOUT, "engine", "text", "src", "shaper.cpp"
)

_ABIS = ("arm64-v8a", "x86_64")
_SONAME = "libRuntime.so"

# Exact D3 dynamic-symbol list for the `nm -D` delta (SD3). Zero of
# these are present in the pre-run shipped `.so` (ASSUMPTION A-T7-1);
# all three must be present after T4/T7 on the same build.
D3_SYMBOLS = ("hb_shape", "FT_Init_FreeType", "u_getVersion")

# Exact device-half commands the coder/tester runs (never run here):
#   nm -D build-android-<abi>/libRuntime.so | grep -e hb_shape \
#     -e FT_Init_FreeType -e u_getVersion
# Expected delta: empty output before (zero D3 symbols) -> all three
# symbols listed after.
DEVICE_NM_COMMAND = (
    "nm -D build-android-<abi>/libRuntime.so "
    "| grep -e hb_shape -e FT_Init_FreeType -e u_getVersion"
)


def _read_text(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def _ref_rows():
    rows = {}
    with open(_PARITY_REF, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 5:
                continue
            label, _, text_hex, ids_csv, adv_csv = parts
            rows[label] = (text_hex, ids_csv, adv_csv)
    return rows


def _artifact_for(abi):
    root = os.environ.get("ANDROID_BUILD_ROOT", "build-android")
    return os.path.join(_RUN_CHECKOUT, "%s-%s" % (root, abi), _SONAME)


class TestParityRefTable(unittest.TestCase):
    """Checked-in HarfBuzz reference rows the parity proof asserts."""

    def test_ref_file_parses_with_tab_columns(self):
        rows = _ref_rows()
        self.assertGreaterEqual(
            len(rows), 2, "harfbuzz_ref.tsv carries no data rows"
        )

    def test_latin_basic_row_matches_parity_expectation(self):
        rows = _ref_rows()
        self.assertIn("latin_basic", rows)
        text_hex, ids_csv, adv_csv = rows["latin_basic"]
        self.assertEqual(text_hex, "48656C6C6F")  # "Hello" UTF-8
        self.assertEqual(ids_csv, "41,69,70,70,72")
        self.assertEqual(adv_csv, "8.42,7.15,6.98,6.98,7.77")

    def test_deva_matra_row_pins_reorder(self):
        rows = _ref_rows()
        self.assertIn("deva_matra", rows)
        _, ids_csv, adv_csv = rows["deva_matra"]
        ids = ids_csv.split(",")
        # Indic vowel-sign reorder: glyph order differs from codepoint
        # order — a 1:1 codepoint-as-id stub can never satisfy this.
        self.assertEqual(ids, ["1502", "1501"])
        self.assertEqual(adv_csv, "6.40,11.90")


class TestParitySourceContract(unittest.TestCase):
    """`harfbuzz_parity_test.cpp` stays hermetic C++17 (unit half)."""

    def test_parity_source_is_stdlib_only(self):
        text = _read_text(_PARITY_CPP)
        self.assertNotIn("jni.h", text)
        self.assertNotIn("android/", text)
        self.assertNotIn("sys/socket", text)
        self.assertNotIn("curl", text)

    def test_parity_source_asserts_reference_ids(self):
        text = _read_text(_PARITY_CPP)
        for token in ("41, 69, 70, 70, 72", "1502", "1501", "8.42f"):
            self.assertIn(
                token, text,
                "parity source lost its reference expectation: %s" % token,
            )

    def test_parity_source_has_exit_code_main(self):
        text = _read_text(_PARITY_CPP)
        self.assertIn("int main()", text)
        self.assertIn("return 1", text)
        self.assertIn("return 0", text)

    def test_parity_test_registered_in_ctest(self):
        text = _read_text(_TEXT_CMAKE)
        self.assertIn(
            "add_test(NAME harfbuzz_parity_test "
            "COMMAND harfbuzz_parity_test)",
            text,
        )


class TestGateSelfTests(unittest.TestCase):
    """Gate self-test halves of the proof Interface (exit 0)."""

    def test_production_link_self_test_exits_zero(self):
        self.assertEqual(_production_link_check.run_self_test(), 0)

    def test_seam_self_test_exits_zero(self):
        self.assertEqual(_seam_check.run_self_test(), 0)

    def test_admission_self_test_exits_zero(self):
        self.assertEqual(_admission_check.run_self_test(), 0)

    def test_recorded_manifest_passes_production_link(self):
        violations = _production_link_check.check_production_links(
            _MANIFEST
        )
        self.assertEqual(violations, [])

    def test_recorded_manifest_passes_seam(self):
        violations = _seam_check.check_seams(_MANIFEST)
        self.assertEqual(violations, [])

    def test_d3_rows_keep_the_text_seam(self):
        entries = load_manifest(_MANIFEST)
        d3 = [
            e for e in entries
            if isinstance(e, dict)
            and e.get("name") in ("HarfBuzz", "ICU4C", "FreeType")
        ]
        self.assertEqual(len(d3), 3, "D3 manifest rows absent (T5)")
        for entry in d3:
            self.assertEqual(entry.get("owner"), "text")
            # Text-pipeline seams per the gate's known-seam set:
            # HarfBuzz TextShaper, ICU TextI18n, FreeType GlyphRaster.
            self.assertIn(
                entry.get("interface"), _seam_check.KNOWN_SEAMS
            )


class TestWeakProbes(unittest.TestCase):
    """D3 weak-probe presence in `shaper.cpp` (unit half of `nm`)."""

    def test_all_three_weak_symbols_declared(self):
        text = _read_text(_SHAPER_CPP)
        for symbol in D3_SYMBOLS:
            self.assertIn(
                "void %s() __attribute__((weak));" % symbol, text,
                "weak probe missing for %s" % symbol,
            )

    def test_real_stack_probe_null_checks_all_three(self):
        text = _read_text(_SHAPER_CPP)
        for symbol in D3_SYMBOLS:
            self.assertIn("%s != nullptr" % symbol, text)


class TestNmDelta(unittest.TestCase):
    """`nm` delta on the real Android `.so` (device half, EXTERNAL).

    Green iff every ABI artifact exists and `nm -D` lists all three
    D3 symbols. Red otherwise, naming the exact device command —
    the coder/tester runs the Android build + `nm` (owning layer:
    tester/T7 device work); this file never runs a device build.
    """

    def test_d3_symbols_present_in_shipped_so(self):
        missing_artifacts = [
            abi for abi in _ABIS
            if not os.path.isfile(_artifact_for(abi))
        ]
        if missing_artifacts:
            self.fail(
                "Android artifacts absent for %s; run the device-half "
                "build, then: %s (AC-DEPS-019/021; "
                "ASSUMPTION A-T7-1: pre-run .so holds zero D3 symbols)"
                % (",".join(missing_artifacts), DEVICE_NM_COMMAND)
            )
        try:
            import shutil
            if shutil.which("nm") is None:
                self.fail(
                    "nm unavailable to verify the D3 delta; run: %s"
                    % DEVICE_NM_COMMAND
                )
            for abi in _ABIS:
                proc = subprocess.run(
                    ["nm", "-D", _artifact_for(abi)],
                    capture_output=True, text=True, timeout=120,
                )
                self.assertEqual(
                    proc.returncode, 0,
                    "nm -D failed on %s" % _artifact_for(abi),
                )
                for symbol in D3_SYMBOLS:
                    self.assertIn(
                        symbol, proc.stdout,
                        "D3 symbol %s absent from %s "
                        "(zero-before -> present-after delta unproven)"
                        % (symbol, _artifact_for(abi)),
                    )
        except OSError as exc:
            self.fail("nm inspection failed: %s" % exc)


if __name__ == "__main__":
    unittest.main()
