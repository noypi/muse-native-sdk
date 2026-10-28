"""Performance budget manifest loader (NFR-PERFORMANCE-003, T1).

Public boundary (plan Checklist T1/T2)::

    load_budgets(path: str) -> dict
    check_coverage(manifest: dict) -> list[str]
    BudgetError(area: str, reason: str)

Schema of ``benchmark/budgets.json``::

    {version, reference_apps[],
     budgets[{area, subsystem, threshold, unit, reference_app}]}

Every one of the nine areas must have at least one row per reference
app, and every row must name an owner subsystem. Gaps raise
``BudgetError`` (fail closed).
"""

import json
import os

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


class BudgetError(Exception):
    """Raised when a budget manifest is missing or has a gap.

    ``area`` names the offending area (or file-level pseudo-area);
    ``reason`` explains the gap.
    """

    def __init__(self, area, reason):
        super().__init__("%s: %s" % (area, reason))
        self.area = area
        self.reason = reason


def check_coverage(manifest):
    """Return the list of missing ``(area x app)`` pairs, [] when full.

    Coverage follows the manifest's own ``reference_apps`` list (plan
    A1: the list is extensible), crossed with the fixed nine areas.
    Each entry names both the area and the app.
    """
    if not isinstance(manifest, dict):
        return ["invalid manifest: expected an object"]
    apps = manifest.get("reference_apps")
    rows = manifest.get("budgets")
    if not isinstance(apps, list) or not isinstance(rows, list):
        return ["invalid manifest: expected reference_apps/budgets lists"]
    present = set()
    for row in rows:
        if isinstance(row, dict):
            present.add((row.get("area"), row.get("reference_app")))
    return [
        "%s x %s" % (area, app)
        for area in NINE_AREAS
        for app in apps
        if (area, app) not in present
    ]


def load_budgets(path):
    """Parse the manifest at ``path``; raise ``BudgetError`` on any gap."""
    if not os.path.isfile(path):
        raise BudgetError("manifest", "missing budget file: %s" % path)
    try:
        with open(path, encoding="utf-8") as handle:
            doc = json.load(handle)
    except (OSError, ValueError) as exc:
        raise BudgetError("manifest", "unparseable budget file: %s" % exc)
    if not isinstance(doc, dict):
        raise BudgetError("manifest", "budget manifest must be an object")
    apps = doc.get("reference_apps")
    if not isinstance(apps, list) or not apps:
        raise BudgetError("manifest", "missing reference_apps list")
    rows = doc.get("budgets")
    if not isinstance(rows, list):
        raise BudgetError("manifest", "budgets must be a list")
    for index, row in enumerate(rows):
        area = row.get("area", "manifest") if isinstance(row, dict) else "manifest"
        if not isinstance(row, dict):
            raise BudgetError(area, "budget row %d must be an object" % index)
        owner = row.get("subsystem")
        if not isinstance(owner, str) or not owner.strip():
            raise BudgetError(
                row.get("area", "manifest"),
                "row %d for area %r missing owner subsystem"
                % (index, row.get("area")),
            )
        if "threshold" not in row or row.get("threshold") is None:
            raise BudgetError(
                row.get("area", "manifest"),
                "row %d for area %r missing threshold"
                % (index, row.get("area")),
            )
    missing = check_coverage(doc)
    if missing:
        area = missing[0].split(" x ")[0]
        raise BudgetError(
            area, "missing budget for %s" % (", ".join(missing),)
        )
    return doc
