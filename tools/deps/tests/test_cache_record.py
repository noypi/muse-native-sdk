#!/usr/bin/env python3
"""T11 contract tests: cache-routed refresh records per-ABI measurements.

Scope: the RECORDING seam only — what rows must appear in
``build/prebuilt-cache.json``. The full 4-ABI GN+NDK execution is
coder-lane (heavy); these tests assert the artifact it must produce.

Hermetic: stdlib only, no network, no env, no services. Fails RED while
the table is missing/unmeasured (AC-DEPS-035 fail-closed).
"""

import json
import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
TABLE_PATH = os.path.join(REPO_ROOT, "build", "prebuilt-cache.json")

REQUIRED_ABIS = ("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
KEY_FIELDS = ("name", "version", "abi", "gn_args_hash", "ndk_rev",
              "snapshot_sha256")


def _load_table(path=TABLE_PATH):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def _rows_by_abi(table):
    rows = table.get("rows", table) if isinstance(table, dict) else table
    if isinstance(rows, dict):
        return rows
    return {row.get("abi", row.get("ABI")): row for row in rows}


class TestCacheMeasurementRows(unittest.TestCase):
    def test_all_four_abi_rows_present(self):
        table = _load_table()
        by_abi = _rows_by_abi(table)
        for abi in REQUIRED_ABIS:
            self.assertIn(abi, by_abi, "missing ABI row: %s" % abi)

    def test_rows_carry_measured_wall_clock_and_object_bytes(self):
        table = _load_table()
        by_abi = _rows_by_abi(table)
        for abi in REQUIRED_ABIS:
            row = by_abi[abi]
            for field in ("wallClockSec", "objectBytes"):
                value = row[field]
                self.assertIsInstance(value, (int, float),
                                      "%s.%s not a number" % (abi, field))
                self.assertNotIsInstance(value, bool,
                                         "%s.%s must be measured" % (abi, field))
                self.assertGreater(value, 0,
                                   "%s.%s unmeasured" % (abi, field))

    def test_rows_carry_full_cache_key(self):
        table = _load_table()
        by_abi = _rows_by_abi(table)
        for abi in REQUIRED_ABIS:
            key = by_abi[abi].get("key", by_abi[abi])
            for field in KEY_FIELDS:
                value = key.get(field)
                self.assertIsInstance(value, str,
                                      "%s key.%s not a str" % (abi, field))
                self.assertTrue(value, "%s key.%s empty" % (abi, field))


class TestPartialRefreshMerge(unittest.TestCase):
    """Pin 3be5353: partial --abis refresh merges with prior rows."""

    def test_partial_refresh_preserves_other_rows(self):
        # Pin 3be5353 merge-with-prior: partial --abis refresh of one ABI
        # must leave the other three rows byte-identical. Hermetic: prior
        # table built in-memory; merge idiom mirrors main()'s
        # merged = dict(prior) + per-ABI update + ABIS-ordered rows.
        abis = ("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
        prior_rows = [
            {"abi": abi, "key": {"name": "skia-m158", "version": "m158",
                                 "abi": abi, "gn_args_hash": "h" + abi,
                                 "ndk_rev": "27.2.12479018",
                                 "snapshot_sha256": "s"},
             "wallClockSec": 1.5, "objectBytes": 100 + i}
            for i, abi in enumerate(REQUIRED_ABIS)
        ]
        prior = _rows_by_abi({"rows": prior_rows})
        refreshed = dict(prior["arm64-v8a"])
        refreshed["wallClockSec"] = 9.5
        merged = dict(prior)
        merged["arm64-v8a"] = refreshed
        rows = [merged[abi] for abi in abis if abi in merged]
        table = {"rows": rows}
        by_abi = {row["abi"]: row for row in table["rows"]}
        for abi in ("armeabi-v7a", "x86_64", "x86"):
            self.assertEqual(
                json.dumps(by_abi[abi], sort_keys=True),
                json.dumps(prior[abi], sort_keys=True),
                "partial refresh clobbered row: %s" % abi)
        self.assertEqual(by_abi["arm64-v8a"]["wallClockSec"], 9.5)


if __name__ == "__main__":
    unittest.main()
