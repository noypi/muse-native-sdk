"""Batch 1 — performance budget manifest loader + coverage (T1 + T2).

Covers NFR-PERFORMANCE-003 (AC-PERFORMANCE-003) and NFR-PERFORMANCE-006
(AC-PERFORMANCE-006..AC-PERFORMANCE-011) per specs-performance.md:
``benchmark/budgets.json`` carries a row for each of the nine areas for
every reference app, every row names an owner subsystem, and the rows
encode the AC-006..AC-011 threshold numbers verbatim.

Public boundary under test (plan Checklist T1/T2)::
    load_budgets(path: str) -> dict
    check_coverage(manifest: dict) -> list[str]
    BudgetError(area: str, reason: str)

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
The committed-file tests read only the artifact under test
(``benchmark/budgets.json``); tmp-manifest tests never touch the repo tree.

Expected state before implementation: RED — neither
``benchmark/budgets.py`` (loader module) nor ``benchmark/budgets.json``
exists, so every test fails (ModuleNotFoundError / missing file).
"""

import json
import os
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COMMITTED_MANIFEST = os.path.join(REPO_ROOT, "benchmark", "budgets.json")

# ASSUMPTION (Q-PERFORMANCE-001): the nine areas named in
# NFR-PERFORMANCE-003 ("startup, first frame, frame time p50/p99, jank
# rate, memory, APK/install size, battery, network and thermal") map to
# these slugs. "frame time p50/p99" is one area; battery, network and
# thermal are three.
NINE_AREAS = [
    "startup",
    "first_frame",
    "frame",
    "jank_rate",
    "memory",
    "size",
    "battery",
    "network",
    "thermal",
]

# ASSUMPTION (Q-PERFORMANCE-001): reference_apps is extensible per plan A1;
# tests use a two-app fixture and read the manifest's own reference_apps
# list rather than a fixed matrix.
FIXTURE_APPS = ["minimal", "gallery"]


def import_budgets():
    """Import the loader module through the public boundary."""
    from benchmark.budgets import BudgetError, check_coverage, load_budgets
    return BudgetError, check_coverage, load_budgets


def make_manifest(apps=None, areas=None, omit=(), owner="runtime",
                  threshold=1.0, unit="ms", version=1):
    """Build an in-memory manifest per the plan schema.

    Schema: {version, reference_apps[], budgets[{area, subsystem,
    threshold, unit, reference_app}]}. ``omit`` holds (area, app) pairs
    to leave out.
    """
    apps = list(apps if apps is not None else FIXTURE_APPS)
    areas = list(areas if areas is not None else NINE_AREAS)
    rows = [
        {
            "area": area,
            "subsystem": owner,
            "threshold": threshold,
            "unit": unit,
            "reference_app": app,
        }
        for area in areas
        for app in apps
        if (area, app) not in omit
    ]
    return {"version": version, "reference_apps": apps, "budgets": rows}


def write_manifest_doc(doc):
    """Write a manifest doc (dict or raw text) to a tmp file; return path."""
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    if isinstance(doc, str):
        tmp.write(doc)
    else:
        json.dump(doc, tmp)
    tmp.close()
    return tmp.name


def numbers_in(value):
    """Collect every numeric value nested in a row (tolerant buddy check)."""
    found = []
    if isinstance(value, (int, float)):
        found.append(value)
    elif isinstance(value, dict):
        for item in value.values():
            found.extend(numbers_in(item))
    elif isinstance(value, (list, tuple)):
        for item in value:
            found.extend(numbers_in(item))
    return found


def read_committed_rows(test_case):
    """Parse the committed manifest with stdlib json only (no loader)."""
    with open(COMMITTED_MANIFEST, encoding="utf-8") as handle:
        doc = json.load(handle)
    test_case.assertIsInstance(doc, dict)
    apps = doc.get("reference_apps")
    rows = doc.get("budgets")
    test_case.assertIsInstance(apps, list)
    test_case.assertIsInstance(rows, list)
    return apps, rows


class TestLoadBudgets(unittest.TestCase):
    """T1 loader: valid manifest parses; gaps raise BudgetError (TC-003)."""

    def test_valid_manifest_returns_dict(self):
        """A complete manifest parses to a dict (AC-PERFORMANCE-003)."""
        _, _, load_budgets = import_budgets()
        path = write_manifest_doc(make_manifest())
        try:
            manifest = load_budgets(path)
        finally:
            os.unlink(path)
        self.assertIsInstance(manifest, dict)
        self.assertIn("budgets", manifest)
        self.assertIn("reference_apps", manifest)

    def test_missing_file_raises_budget_error(self):
        """A missing path raises BudgetError, not bare OSError."""
        BudgetError, _, load_budgets = import_budgets()
        with self.assertRaises(BudgetError):
            load_budgets(os.path.join(REPO_ROOT, "does-not-exist-xyz.json"))

    def test_invalid_json_raises_budget_error(self):
        """Unparseable JSON raises BudgetError (fail closed)."""
        BudgetError, _, load_budgets = import_budgets()
        path = write_manifest_doc("{ this is not json !!!")
        try:
            with self.assertRaises(BudgetError):
                load_budgets(path)
        finally:
            os.unlink(path)

    def test_non_object_manifest_raises_budget_error(self):
        """A JSON list at the top level raises BudgetError (input validation)."""
        BudgetError, _, load_budgets = import_budgets()
        path = write_manifest_doc([{"area": "startup"}])
        try:
            with self.assertRaises(BudgetError):
                load_budgets(path)
        finally:
            os.unlink(path)

    def test_budgets_not_list_raises_budget_error(self):
        """A non-list budgets entry raises BudgetError (input validation)."""
        BudgetError, _, load_budgets = import_budgets()
        doc = make_manifest()
        doc["budgets"] = {"area": "startup"}
        path = write_manifest_doc(doc)
        try:
            with self.assertRaises(BudgetError):
                load_budgets(path)
        finally:
            os.unlink(path)

    def test_missing_area_raises_budget_error_naming_area(self):
        """An area with no row for an app raises BudgetError naming it."""
        BudgetError, _, load_budgets = import_budgets()
        path = write_manifest_doc(make_manifest(omit={("thermal", "gallery")}))
        try:
            with self.assertRaises(BudgetError) as ctx:
                load_budgets(path)
        finally:
            os.unlink(path)
        self.assertIn("thermal", str(ctx.exception).lower())

    def test_row_missing_owner_raises_budget_error(self):
        """A row with no owner subsystem raises BudgetError (AC-003)."""
        BudgetError, _, load_budgets = import_budgets()
        doc = make_manifest()
        del doc["budgets"][0]["subsystem"]
        path = write_manifest_doc(doc)
        try:
            with self.assertRaises(BudgetError) as ctx:
                load_budgets(path)
        finally:
            os.unlink(path)
        self.assertIn("owner", str(ctx.exception).lower())

    def test_row_empty_owner_raises_budget_error(self):
        """A row with an empty owner subsystem raises BudgetError."""
        BudgetError, _, load_budgets = import_budgets()
        doc = make_manifest()
        doc["budgets"][0]["subsystem"] = "  "
        path = write_manifest_doc(doc)
        try:
            with self.assertRaises(BudgetError):
                load_budgets(path)
        finally:
            os.unlink(path)

    def test_row_missing_threshold_raises_budget_error(self):
        """A row with no threshold raises BudgetError."""
        BudgetError, _, load_budgets = import_budgets()
        doc = make_manifest()
        del doc["budgets"][0]["threshold"]
        path = write_manifest_doc(doc)
        try:
            with self.assertRaises(BudgetError):
                load_budgets(path)
        finally:
            os.unlink(path)

    def test_budget_error_carries_area_and_reason(self):
        """BudgetError exposes .area and .reason attributes."""
        BudgetError, _, _ = import_budgets()
        err = BudgetError("startup", "missing threshold")
        self.assertEqual(err.area, "startup")
        self.assertEqual(err.reason, "missing threshold")

    def test_committed_manifest_exists(self):
        """benchmark/budgets.json exists (TC-PERFORMANCE-003, no loader)."""
        self.assertTrue(
            os.path.isfile(COMMITTED_MANIFEST),
            msg="benchmark/budgets.json is missing",
        )

    def test_committed_manifest_covers_nine_areas_per_app(self):
        """Every one of the nine areas has >=1 row per reference app and
        every row names an owner subsystem (AC-PERFORMANCE-003)."""
        apps, rows = read_committed_rows(self)
        self.assertGreaterEqual(len(apps), 1)
        for row in rows:
            self.assertTrue(row.get("subsystem"),
                            msg="row without owner subsystem: %r" % (row,))
        for area in NINE_AREAS:
            for app in apps:
                self.assertTrue(
                    any(r.get("area") == area
                        and r.get("reference_app") == app for r in rows),
                    msg="missing (%s x %s)" % (area, app),
                )


class TestCheckCoverage(unittest.TestCase):
    """T2: check_coverage lists missing (area x app) pairs, [] when full."""

    def test_complete_manifest_zero_gaps(self):
        """A full manifest reports zero gaps."""
        _, check_coverage, _ = import_budgets()
        self.assertEqual(check_coverage(make_manifest()), [])

    def test_missing_pair_is_named(self):
        """Each missing (area x app) pair is named in the result."""
        _, check_coverage, _ = import_budgets()
        missing = check_coverage(make_manifest(omit={("thermal", "gallery")}))
        self.assertEqual(len(missing), 1)
        self.assertIn("thermal", missing[0].lower())
        self.assertIn("gallery", missing[0].lower())

    def test_multiple_gaps_all_named(self):
        """Every gap is reported, not just the first."""
        _, check_coverage, _ = import_budgets()
        omit = {("startup", "minimal"), ("network", "gallery")}
        missing = check_coverage(make_manifest(omit=omit))
        self.assertEqual(len(missing), 2)
        combined = " ".join(missing).lower()
        self.assertIn("startup", combined)
        self.assertIn("network", combined)

    def test_empty_budgets_lists_all_pairs(self):
        """An empty budgets list reports every (area x app) pair."""
        _, check_coverage, _ = import_budgets()
        doc = make_manifest()
        doc["budgets"] = []
        missing = check_coverage(doc)
        self.assertEqual(len(missing), len(NINE_AREAS) * len(FIXTURE_APPS))

    def test_reference_apps_list_is_extensible(self):
        """Coverage follows the manifest's own reference_apps list
        (ASSUMPTION per plan A1 / Q-PERFORMANCE-001)."""
        _, check_coverage, _ = import_budgets()
        doc = make_manifest(apps=["minimal", "gallery", "extra-app"])
        self.assertEqual(check_coverage(doc), [])
        missing = check_coverage(
            make_manifest(apps=["minimal", "gallery", "extra-app"],
                          omit={("size", "extra-app")}))
        self.assertEqual(len(missing), 1)
        self.assertIn("extra-app", missing[0])


class TestThresholdSpotChecks(unittest.TestCase):
    """T2: committed rows carry the AC-006..AC-011 numbers verbatim."""

    def check_numbers(self, area, expected):
        """Rows for ``area`` contain each expected number somewhere."""
        _, rows = read_committed_rows(self)
        area_rows = [r for r in rows if r.get("area") == area]
        self.assertTrue(area_rows, msg="no rows for area %r" % (area,))
        found = []
        for row in area_rows:
            found.extend(numbers_in(row.get("threshold")))
            found.extend(numbers_in(row.get("thresholds")))
            found.extend(numbers_in(row))
        for number in expected:
            self.assertIn(
                number, found,
                msg="area %r rows lack number %r (found %r)"
                % (area, number, sorted(set(found))),
            )

    def test_startup_cold_envelope(self):
        """Startup rows carry the 50-80 ms cold envelope (AC-006)."""
        self.check_numbers("startup", [50, 80])

    def test_frame_typical_and_complex(self):
        """Frame rows carry typical <4 ms and complex <8 ms (AC-007)."""
        self.check_numbers("frame", [4, 8])

    def test_memory_rss_and_engine(self):
        """Memory rows carry RSS 25-35 MB, engine 12-15 MB (AC-008)."""
        self.check_numbers("memory", [25, 35, 12, 15])

    def test_size_envelope(self):
        """Size rows carry 12-18 MB APK, ~3 MB ICU, 15-25 MB modules,
        35-50 MB installed (AC-009)."""
        self.check_numbers("size", [12, 18, 3, 15, 25, 35, 50])

    def test_latency_budgets(self):
        """Latency rows carry touch 1-2 ms, keyboard 16 ms, back 1 ms,
        scroll 120 FPS (AC-010)."""
        _, rows = read_committed_rows(self)
        latency_rows = [r for r in rows
                        if r.get("area") in ("latency", "frame", "startup")]
        self.assertTrue(latency_rows)
        found = []
        for row in latency_rows:
            found.extend(numbers_in(row))
        for number in (1, 2, 16, 120):
            self.assertIn(number, found,
                          msg="latency budget lacks %r (found %r)"
                          % (number, sorted(set(found))))

    def test_battery_network_thermal(self):
        """Battery/network/thermal rows exist with owners; network rows
        carry the HTTP <1 ms dispatch budget (AC-011)."""
        apps, rows = read_committed_rows(self)
        for area in ("battery", "network", "thermal"):
            area_rows = [r for r in rows if r.get("area") == area]
            self.assertTrue(area_rows, msg="no rows for area %r" % (area,))
            for row in area_rows:
                self.assertTrue(row.get("subsystem"))
        net_found = []
        for row in rows:
            if row.get("area") == "network":
                net_found.extend(numbers_in(row))
        self.assertIn(1, net_found)


if __name__ == "__main__":
    unittest.main()
