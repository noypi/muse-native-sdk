#!/usr/bin/env python3
"""Batch 3 (T5, T6) hermetic records-and-gates tests for `vendor-d3`.

Covers plan.md Batch 3 "Records & gates":
- T5 (plan Interface: `manifestEntry(name, version, sha256, url, licence,
  licence_url) -> SBOM row`): one manifest row each for HarfBuzz 15.0.0,
  ICU 79.1, FreeType 2.15.0 with request hashes + official origins +
  licence provenance; per-ABI slots in `toolchain/deps/pins.json` (confirmed
  as the per-ABI locked-setup manifest: keys arm64-v8a, armeabi-v7a, x86,
  x86_64); D3 present in `app/src/main/assets/third-party-notices.txt`;
  generated `dist/license-report.json` carries the D3 rows.
- T6 (plan Interface: `licence_check(manifest) -> exit 0` with D3 licences
  `HarfBuzz: Old-MIT/BSD-style`, `ICU: Unicode`, `FreeType: FTL`): the gate
  allows the recorded D3 licences; no LGPL path is triggered (static link
  stays lawful); an unidentifiable licence fails deny-by-default.

Hermetic: Python stdlib `unittest` + `os`/`json`/`sys` only, plus the
in-checkout `tools.deps` modules under test (imported, never executed as
subprocesses). Read-only file assertions and tmp-manifest gate calls; no
network, no secrets, no application config, no live service, no
subprocess. These tests FAIL RED until the coder records the D3 rows; the
SBOM/notices generation itself is never a network step.

Per GR-08 T1.8 this file is assigned to the first item (T5) and mapped
in both unit-tests/T5.md and unit-tests/T6.md.
"""

import json
import os
import sys
import tempfile
import unittest

sys.path.insert(
    0,
    os.path.dirname(
        os.path.dirname(
            os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        )
    ),
)

from tools.deps import lgpl_check as _lgpl_check
from tools.deps import licence_check as _licence_check
from tools.deps import manifest_check as _manifest_check
from tools.deps._deps_data import get_allowlist, load_manifest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_RUN_CHECKOUT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)

_MANIFEST = os.path.join(_RUN_CHECKOUT, "build", "dependency-manifest.json")
_PINS = os.path.join(_RUN_CHECKOUT, "toolchain", "deps", "pins.json")
_NOTICES = os.path.join(
    _RUN_CHECKOUT, "app", "src", "main", "assets",
    "third-party-notices.txt",
)
_LICENSE_REPORT = os.path.join(
    _RUN_CHECKOUT, "dist", "license-report.json"
)

_ABIS = ("arm64-v8a", "armeabi-v7a", "x86", "x86_64")

# Expected D3 coordinates verbatim from workflow.local/runs/vendor-d3/run.md.
EXPECTED = {
    "HarfBuzz": {
        "version": "15.0.0",
        "sha256": (
            "968536e9abd782f07f7954044c0c4020199fcdd39b41d221"
            "ec42d2a3be4a44df"
        ),
    },
    "ICU4C": {
        "version": "79.1",
        "sha256": (
            "06fa63ecb521048f8082f12ad988f6b678d90cbd88aafce24"
            "cbc94902b191d99"
        ),
    },
    "FreeType": {
        "version": "2.15.0",
        "sha256": (
            "9186627467a616d9a9e3e574748ac0c45fa77398e6364edef"
            "3041a8ba8d92f64"
        ),
    },
}

# Plan T6 licence strings (Interface line verbatim).
PLAN_LICENCES = {
    "HarfBuzz": "Old-MIT/BSD-style",
    "ICU4C": "Unicode",
    "FreeType": "FTL",
}


def _manifest_entries():
    return load_manifest(_MANIFEST)


def _d3_rows():
    return [
        entry for entry in _manifest_entries()
        if isinstance(entry, dict) and entry.get("name") in EXPECTED
    ]


def _row(name):
    for entry in _d3_rows():
        if entry.get("name") == name:
            return entry
    return None


def _tmp_manifest_path(entries):
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(entries, tmp)
    tmp.close()
    return tmp.name


def _gate_entry(name, licence="MIT", **overrides):
    data = {
        "name": name,
        "owner": "text",
        "interface": "TextEngine",
        "exit_plan": "replace with a local implementation",
        "licence": licence,
        "link": "static",
    }
    data.update(overrides)
    return data


def find_notices_gaps(entries, notices_body):
    """Return manifest entry names absent from the notices text."""
    return [
        entry.get("name") for entry in entries
        if isinstance(entry, dict) and entry.get("name")
        and entry.get("name") not in notices_body
    ]


class TestT5ManifestD3Rows(unittest.TestCase):
    """T5: SBOM rows for HarfBuzz/ICU4C/FreeType in the manifest."""

    def test_manifest_parses_with_dependencies(self):
        entries = _manifest_entries()
        self.assertIsInstance(entries, list)
        self.assertTrue(entries)

    def test_d3_rows_present_owned_by_text(self):
        rows = _d3_rows()
        self.assertEqual(
            sorted(entry["name"] for entry in rows),
            ["FreeType", "HarfBuzz", "ICU4C"],
        )
        for entry in rows:
            self.assertEqual(entry.get("owner"), "text")

    def test_d3_versions_exact(self):
        for name, want in EXPECTED.items():
            row = _row(name)
            self.assertIsNotNone(row, "missing manifest row for %s" % name)
            self.assertEqual(
                row.get("version"), want["version"],
                "%s version must be %s" % (name, want["version"]),
            )

    def test_d3_sha256_match_request_hashes(self):
        for name, want in EXPECTED.items():
            row = _row(name)
            self.assertIsNotNone(row, "missing manifest row for %s" % name)
            self.assertEqual(
                row.get("sha256"), want["sha256"],
                "%s sha256 must equal the request hash" % name,
            )

    def test_d3_urls_are_official_https_origins(self):
        for name in EXPECTED:
            row = _row(name)
            self.assertIsNotNone(row, "missing manifest row for %s" % name)
            url = row.get("url")
            self.assertIsInstance(url, str)
            self.assertTrue(url, "%s url must be recorded" % name)
            self.assertTrue(
                url.startswith("https://"),
                "%s url must be an https origin, got %r" % (name, url),
            )

    def test_d3_licence_urls_recorded(self):
        for name in EXPECTED:
            row = _row(name)
            self.assertIsNotNone(row, "missing manifest row for %s" % name)
            url = row.get("licence_url")
            self.assertIsInstance(url, str)
            self.assertTrue(url, "%s licence_url must be recorded" % name)
            self.assertTrue(
                url.startswith("https://"),
                "%s licence_url must be https, got %r" % (name, url),
            )

    def test_icu_licence_is_unicode(self):
        row = _row("ICU4C")
        self.assertIsNotNone(row, "missing manifest row for ICU4C")
        self.assertEqual(row.get("licence"), "Unicode")

    def test_freetype_licence_is_ftl(self):
        row = _row("FreeType")
        self.assertIsNotNone(row, "missing manifest row for FreeType")
        self.assertEqual(row.get("licence"), "FTL")

    def test_harfbuzz_licence_provenance_is_old_mit(self):
        # Plan records HarfBuzz as Old-MIT/BSD-style; the gate (run, not
        # edited) only admits exact allow-list identifiers, so the row must
        # carry a gate-allowed licence AND name the Old-MIT provenance in
        # the licence field or its licence_url (ASSUMPTION A-T5-1).
        row = _row("HarfBuzz")
        self.assertIsNotNone(row, "missing manifest row for HarfBuzz")
        allowed, _ = _licence_check.classify_licence(row.get("licence"))
        self.assertTrue(
            allowed,
            "HarfBuzz licence %r must pass the licence gate" % (
                row.get("licence"),),
        )
        provenance = "%s %s" % (row.get("licence"), row.get("licence_url"))
        self.assertTrue(
            "old-mit" in provenance.lower()
            or "bsd" in provenance.lower(),
            "HarfBuzz row must name the Old-MIT/BSD-style provenance, "
            "got %r" % (provenance,),
        )

    def test_d3_rows_pass_manifest_check(self):
        gaps = _manifest_check.find_gaps(_d3_rows())
        self.assertEqual(gaps, [])


class TestT5PerAbiPins(unittest.TestCase):
    """T5: D3 slots in the per-ABI locked-setup manifest (pins.json)."""

    def _pins(self):
        with open(_PINS, "r", encoding="utf-8") as handle:
            return json.load(handle)

    def test_pins_has_all_four_abi_slots(self):
        pins = self._pins()
        for abi in _ABIS:
            self.assertIn(abi, pins, "pins.json missing ABI slot %s" % abi)

    def test_each_abi_carries_d3_hashes(self):
        pins = self._pins()
        for abi in _ABIS:
            hashes = {
                entry.get("sha256") for entry in pins.get(abi, [])
                if isinstance(entry, dict)
            }
            for name, want in EXPECTED.items():
                self.assertIn(
                    want["sha256"], hashes,
                    "%s slot missing D3 %s hash" % (abi, name),
                )

    def test_each_abi_d3_versions_match_manifest(self):
        pins = self._pins()
        for abi in _ABIS:
            by_hash = {
                entry.get("sha256"): entry for entry in pins.get(abi, [])
                if isinstance(entry, dict)
            }
            for name, want in EXPECTED.items():
                entry = by_hash.get(want["sha256"])
                self.assertIsNotNone(
                    entry, "%s slot missing D3 %s row" % (abi, name))
                self.assertEqual(
                    entry.get("version"), want["version"],
                    "%s slot %s version must be %s" % (
                        abi, name, want["version"]),
                )
                self.assertTrue(
                    isinstance(entry.get("url"), str)
                    and entry.get("url").startswith("https://"),
                    "%s slot %s url must be an https origin" % (abi, name),
                )


class TestT5Notices(unittest.TestCase):
    """T5: third-party notices list every D3 dep (AC-DEPS-015)."""

    def _notices(self):
        with open(_NOTICES, "r", encoding="utf-8") as handle:
            return handle.read()

    def test_notices_name_all_d3_deps(self):
        body = self._notices()
        for name in ("HarfBuzz", "ICU4C", "FreeType"):
            self.assertIn(name, body, "notices missing %s" % name)

    def test_notices_name_d3_licence_strings(self):
        body = self._notices()
        self.assertTrue(
            "Old-MIT" in body or "old-mit" in body.lower(),
            "notices must name the HarfBuzz Old-MIT provenance",
        )
        self.assertIn("Unicode", body, "notices missing the ICU licence")
        self.assertIn("FTL", body, "notices missing the FreeType licence")

    def test_notices_gap_scan_is_empty_for_d3(self):
        body = self._notices()
        self.assertEqual(find_notices_gaps(_d3_rows(), body), [])

    def test_notices_gap_scan_flags_a_removed_dep(self):
        # Closest observable half of "absent from notices fails the build":
        # the scan the build gate must run flags a D3 name removed from the
        # notices text (ASSUMPTION A-T5-2: no notices-coverage script exists
        # yet; the coder wires this scan into the build gate).
        body = self._notices().replace("HarfBuzz", "Redacted-xyz")
        self.assertEqual(
            find_notices_gaps(_d3_rows(), body), ["HarfBuzz"])


class TestT5SbomReport(unittest.TestCase):
    """T5: generated SBOM report carries the D3 rows."""

    def test_license_report_exists_and_parses(self):
        self.assertTrue(
            os.path.isfile(_LICENSE_REPORT),
            "dist/license-report.json must be generated: %s"
            % _LICENSE_REPORT,
        )
        with open(_LICENSE_REPORT, "r", encoding="utf-8") as handle:
            report = json.load(handle)
        self.assertIsInstance(report, dict)

    def test_license_report_carries_d3_rows(self):
        with open(_LICENSE_REPORT, "r", encoding="utf-8") as handle:
            report = json.load(handle)
        text = json.dumps(report)
        for name, want in EXPECTED.items():
            self.assertIn(
                want["sha256"], text,
                "license-report.json missing D3 %s hash" % name)
        for licence in ("Unicode", "FTL"):
            self.assertIn(
                licence, text,
                "license-report.json missing licence %s" % licence)


class TestT5DepsDataUnchanged(unittest.TestCase):
    """T5: checks read D3 allow-list rows from _deps_data (no pin move)."""

    def test_allowlist_names_d3_owned_by_text(self):
        allow = {
            item["name"]: item for item in get_allowlist()
            if item["name"] in ("HarfBuzz", "ICU4C", "FreeType")
        }
        self.assertEqual(
            sorted(allow), ["FreeType", "HarfBuzz", "ICU4C"])
        for name, item in allow.items():
            self.assertEqual(item["owner"], "text", name)

    def test_cronet_precedent_rows_untouched(self):
        # FR-DEPS-010 precedent: per-ABI pins live in pins.json, not in
        # _deps_data; the D3 change must not disturb the Cronet rows.
        from tools.deps._deps_data import get_cronet_pins

        pins = get_cronet_pins()
        self.assertEqual(len(pins), 2)
        self.assertTrue(all("licence_url" in pin for pin in pins))


class TestT6LicenceGate(unittest.TestCase):
    """T6: licence_check admits D3, denies the unknown (deny-by-default)."""

    def test_recorded_harfbuzz_token_allowed_with_old_mit_provenance(self):
        # GAP-T6-1 resolution (a), decided by the user 2026-11-02: the
        # plan-literal `Old-MIT/BSD-style` token is denied by the
        # run-not-edited gate, so HarfBuzz is recorded as the gate-allowed
        # `MIT` token with Old-MIT/BSD-style provenance in `licence_url`
        # and the notices file. This supersedes (not loosens) the
        # plan-literal assertion: the recorded token must classify allowed
        # AND the provenance must be recorded in both places.
        for name, licence in (("ICU4C", "Unicode"),
                              ("FreeType", "FTL")):
            allowed, reason = _licence_check.classify_licence(licence)
            self.assertTrue(
                allowed,
                "recorded licence %r for %s must pass the gate: %s" % (
                    licence, name, reason),
            )
        row = _row("HarfBuzz")
        self.assertIsNotNone(row, "missing manifest row for HarfBuzz")
        allowed, reason = _licence_check.classify_licence(
            row.get("licence"))
        self.assertTrue(
            allowed,
            "recorded HarfBuzz licence %r must pass the gate: %s" % (
                row.get("licence"), reason),
        )
        self.assertIn(
            "old-mit",
            str(row.get("licence_url")).lower(),
            "HarfBuzz licence_url must carry the Old-MIT provenance, "
            "got %r" % (row.get("licence_url"),),
        )
        with open(_NOTICES, "r", encoding="utf-8") as handle:
            notices = handle.read()
        self.assertIn("HarfBuzz", notices, "notices missing HarfBuzz")
        self.assertTrue(
            "Old-MIT" in notices or "old-mit" in notices.lower(),
            "notices must carry the HarfBuzz Old-MIT paragraph",
        )

    def test_recorded_d3_licences_pass_the_gate(self):
        entries = [
            _gate_entry(name, licence=row["licence"])
            for name, row in ((n, _row(n)) for n in EXPECTED)
        ]
        for item in entries:
            self.assertIsNotNone(item)
        denied = _licence_check.find_denied(entries)
        self.assertEqual(denied, [])

    def test_unidentifiable_licence_is_denied(self):
        path = _tmp_manifest_path(
            [_gate_entry("st-weird-xyz", licence="Totally-Custom-1.0-xyz")])
        try:
            denied = _licence_check.check_manifest(path)
        finally:
            os.unlink(path)
        self.assertEqual(len(denied), 1)
        self.assertEqual(denied[0][0], "st-weird-xyz")

    def test_missing_licence_is_denied(self):
        for tag, licence in (("missing", None), ("empty", ""),
                             ("blank", "   ")):
            entry = _gate_entry("st-nolic-%s-xyz" % tag)
            entry["licence"] = licence
            path = _tmp_manifest_path([entry])
            try:
                denied = _licence_check.check_manifest(path)
            finally:
                os.unlink(path)
            self.assertEqual(
                len(denied), 1, "licence %s must be denied" % tag)

    def test_no_lgpl_path_triggered_for_d3(self):
        # Static D3 linkage stays lawful only because no D3 row is LGPL:
        # every recorded D3 licence must be non-LGPL and the LGPL gate
        # must pass vacuously over the D3 rows (AC-DEPS-014 must NOT fire).
        for name in EXPECTED:
            row = _row(name)
            self.assertIsNotNone(row, "missing manifest row for %s" % name)
            self.assertNotIn("lgpl", str(row.get("licence")).lower(), name)
        entries = [
            _gate_entry(name, licence=row["licence"])
            for name, row in ((n, _row(n)) for n in EXPECTED)
        ]
        self.assertEqual(_lgpl_check.find_violations(entries), [])

    def test_self_tests_pass(self):
        self.assertEqual(_licence_check.run_self_test(), 0)
        self.assertEqual(_licence_check.run_self_test_unidentified(), 0)
        self.assertEqual(_lgpl_check.run_self_test(), 0)
        self.assertEqual(_manifest_check.run_self_test(), 0)


class TestNoNetworkByDesign(unittest.TestCase):
    """Hermeticity: this batch test performs file reads + gate calls only."""

    _ALLOWED_IMPORTS = frozenset(
        {"json", "os", "sys", "tempfile", "unittest", "tools"})

    def test_only_hermetic_stdlib_imports(self):
        with open(os.path.abspath(__file__), "r",
                  encoding="utf-8") as handle:
            lines = handle.readlines()
        imported = set()
        for line in lines:
            stripped = line.strip()
            if stripped.startswith("import "):
                imported.add(stripped.split()[1].split(".")[0])
            elif stripped.startswith("from ") and " import " in stripped:
                imported.add(stripped.split()[1].split(".")[0])
        self.assertTrue(imported)
        self.assertLessEqual(imported, self._ALLOWED_IMPORTS,
                             "non-hermetic import in batch test: %s"
                             % sorted(imported - self._ALLOWED_IMPORTS))

    def test_no_network_or_process_tokens_in_test_bodies(self):
        # Scan only the T5/T6 test bodies (not this hermeticity class
        # itself, whose forbidden-token list would trivially self-match).
        # URL-shape assertions below compare fixture strings only; network
        # capability would need socket/urllib/subprocess, all forbidden.
        forbidden = ("socket", "urllib", "curl", "wget",
                     "subprocess", "os.system", "os.popen", "requests")
        with open(os.path.abspath(__file__), "r",
                  encoding="utf-8") as handle:
            source = handle.read()
        start = source.index("class TestT5ManifestD3Rows")
        end = source.index("class TestNoNetworkByDesign")
        lowered = source[start:end].lower()
        for token in forbidden:
            self.assertNotIn(token, lowered,
                             "network-capable token in test body: %s"
                             % token)


if __name__ == "__main__":
    unittest.main()
