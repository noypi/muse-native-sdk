#!/usr/bin/env python3
"""Batch 1 (T1, T2) hermetic staging tests for feature `vendor-d3`.

Covers plan.md Batch 1 "Pins & staging":
- T1 (plan Interface: `pinsFile(path: string) ->
  {harfbuzz:{file,sha256}, icu4c:{file,sha256}, freetype:{file,sha256}}`):
  `tools/deps/third_party_pins.json` D3 rows must equal the run-request
  hashes byte-for-byte (run.md, NOT memory):
    hb-15.0.0  968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df
    icu-79.1   06fa63ecb521048f8082f12ad988f6b678d90cbd88aafce24cbc94902b191d99
    ft-2.15.0  9186627467a616d9a9e3e574748ac0c45fa77398e6364edef3041a8ba8d92f64
- T2 (plan Interface: `stageD3(stagingDir: path) -> {dir, sha256ok: bool}`):
  tarballs exist under the settled sibling dir, re-hashed sha256 matches
  the recorded pins, extracted dirs contain the key files, SHA256SUMS holds
  the three hashes, README ledger rows are present.

Hermetic: Python stdlib `unittest` + `hashlib`/`json`/`os` only. File reads
only; no network, no secrets, no application config, no live service, no
subprocess. Network-freedom is asserted by design (see
TestNoNetworkByDesign, which scans this file's own source for forbidden
network tokens).

Plan Batch 1 names `toolchain/deps/setupVerify.test.js` as its JS-side
test file; this Python file is the code-unit-tester's hermetic batch
companion (plan names no Python batch file, so `test_d3_staging.py` per the
run instruction) and lives in the run checkout so it ships on
branch `run/vendor-d3`.
"""

import hashlib
import json
import os
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_RUN_CHECKOUT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)
_PINS_FILE = os.path.join(_RUN_CHECKOUT, "tools", "deps",
                          "third_party_pins.json")

_THIRD_PARTY_DIR = os.environ.get(
    "MUSE_NATIVE_THIRD_PARTY_DIR",
    "/media/saif/EXTRA/workspaces/meta-hackathon/third-party",
)
_D3VENDOR_DIR = os.path.join(
    "/media/saif/EXTRA/workspaces/meta-hackathon/muse-native-sdk",
    "workflow.local",
    "d3vendor",
)

# Expected pins verbatim from workflow.local/runs/vendor-d3/run.md.
EXPECTED = {
    "harfbuzz": {
        "file": "harfbuzz-15.0.0.tar.xz",
        "sha256": "968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df",
        "extracted": "harfbuzz-15.0.0",
        "key_file": os.path.join("src", "hb-shape.h"),
    },
    "icu4c": {
        "file": "icu4c-79.1-sources.tgz",
        "sha256": "06fa63ecb521048f8082f12ad988f6b678d90cbd88aafce24cbc94902b191d99",
        "extracted": "icu-79.1",
        "key_file": os.path.join("source", "configure"),
    },
    "freetype": {
        "file": "freetype-2.15.0.tar.gz",
        "sha256": "9186627467a616d9a9e3e574748ac0c45fa77398e6364edef3041a8ba8d92f64",
        "extracted": "freetype-2.15.0",
        "key_file": os.path.join("include", "freetype", "freetype.h"),
    },
}


def _sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_pins():
    with open(_PINS_FILE, "r", encoding="utf-8") as handle:
        return json.load(handle)


class TestT1LockedPins(unittest.TestCase):
    """T1: D3 pins/hashes recorded in the committed manifest."""

    def test_pins_file_parses(self):
        pins = _load_pins()
        self.assertIsInstance(pins, dict)

    def test_harfbuzz_pin_matches_request(self):
        pins = _load_pins()
        self.assertEqual(pins["harfbuzz"]["file"],
                         EXPECTED["harfbuzz"]["file"])
        self.assertEqual(pins["harfbuzz"]["sha256"],
                         EXPECTED["harfbuzz"]["sha256"])

    def test_icu4c_pin_matches_request(self):
        pins = _load_pins()
        self.assertEqual(pins["icu4c"]["file"],
                         EXPECTED["icu4c"]["file"])
        self.assertEqual(pins["icu4c"]["sha256"],
                         EXPECTED["icu4c"]["sha256"])

    def test_freetype_pin_matches_request(self):
        pins = _load_pins()
        self.assertEqual(pins["freetype"]["file"],
                         EXPECTED["freetype"]["file"])
        self.assertEqual(pins["freetype"]["sha256"],
                         EXPECTED["freetype"]["sha256"])


class TestT2SourcesStaged(unittest.TestCase):
    """T2: sources staged + ledgered outside the repo."""

    def test_tarballs_exist(self):
        for name, want in EXPECTED.items():
            path = os.path.join(_THIRD_PARTY_DIR, want["file"])
            self.assertTrue(os.path.isfile(path),
                            "missing staged tarball for %s: %s"
                            % (name, path))

    def test_tarball_hashes_match_recorded_pins(self):
        for name, want in EXPECTED.items():
            path = os.path.join(_THIRD_PARTY_DIR, want["file"])
            self.assertEqual(_sha256_of(path), want["sha256"],
                             "sha256 mismatch for %s" % name)

    def test_d3vendor_harfbuzz_tarball_hash_matches(self):
        path = os.path.join(_D3VENDOR_DIR, EXPECTED["harfbuzz"]["file"])
        self.assertTrue(os.path.isfile(path),
                        "missing d3vendor tarball: %s" % path)
        self.assertEqual(_sha256_of(path),
                         EXPECTED["harfbuzz"]["sha256"])

    def test_extracted_dirs_contain_key_files(self):
        for name, want in EXPECTED.items():
            path = os.path.join(_THIRD_PARTY_DIR, want["extracted"],
                                want["key_file"])
            self.assertTrue(os.path.isfile(path),
                            "missing key file for %s: %s" % (name, path))

    def test_sha256sums_contains_all_three_hashes(self):
        with open(os.path.join(_THIRD_PARTY_DIR, "SHA256SUMS"),
                  "r", encoding="utf-8") as handle:
            body = handle.read()
        for name, want in EXPECTED.items():
            self.assertIn(want["sha256"], body,
                          "SHA256SUMS missing %s hash" % name)
            self.assertIn(want["file"], body,
                          "SHA256SUMS missing %s filename" % name)

    def test_readme_ledger_rows_present(self):
        with open(os.path.join(_THIRD_PARTY_DIR, "README.md"),
                  "r", encoding="utf-8") as handle:
            body = handle.read()
        self.assertIn("harfbuzz-15.0.0.tar.xz", body)
        self.assertIn("icu4c-79.1-sources.tgz", body)
        self.assertIn("freetype-2.15.0.tar.gz", body)
        self.assertIn("HarfBuzz", body)
        self.assertIn("ICU4C", body)
        self.assertIn("FreeType", body)


class TestNoNetworkByDesign(unittest.TestCase):
    """Hermeticity: this batch test performs file reads only."""

    _ALLOWED_IMPORTS = frozenset({"hashlib", "json", "os", "unittest"})

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
        # Scan only the T1/T2 test bodies (not this hermeticity class
        # itself, whose forbidden-token list would trivially self-match).
        forbidden = ("socket", "urllib", "http", "curl", "wget",
                     "subprocess", "os.system", "os.popen")
        with open(os.path.abspath(__file__), "r",
                  encoding="utf-8") as handle:
            source = handle.read()
        start = source.index("class TestT1LockedPins")
        end = source.index("class TestNoNetworkByDesign")
        lowered = source[start:end].lower()
        for token in forbidden:
            self.assertNotIn(token, lowered,
                             "network-capable token in test body: %s"
                             % token)


if __name__ == "__main__":
    unittest.main()
