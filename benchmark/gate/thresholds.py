"""Threshold loader for the CI perf gate (single edit point).

Thresholds live in ``benchmark/budgets.json`` and are imported here rather
than duplicated, so a budget change is a single manifest edit (AC-TESTING-028).
``load_thresholds`` maps each budget area to its owning subsystem and gating
budget; composite per-area threshold dicts from the committed manifest are
reduced to one scalar via :data:`_AREA_BUDGET_KEYS`.
"""

import json

# For composite (dict) thresholds in the committed manifest, the key that
# carries the scalar this gate enforces per area. Scalar numeric thresholds
# (as used by gate fixtures) are taken as-is.
_AREA_BUDGET_KEYS = {
    "startup": ("cold_ms",),
    "first_frame": ("first_frame_ms",),
    "frame": ("typical_ms",),
    "jank_rate": ("max_jank_rate",),
    "memory": ("rss_max_mb",),
    "size": ("apk_max_mb",),
    "battery": ("max_drain_mah_per_hour",),
    "network": ("http_dispatch_ms",),
    "thermal": ("sustained_frame_p99_ms",),
}


def _scalar_budget(area, threshold):
    """Reduce a manifest threshold entry to a single gating budget float."""
    if isinstance(threshold, bool):
        raise ValueError(f"non-numeric threshold for area {area!r}")
    if isinstance(threshold, (int, float)):
        return float(threshold)
    if isinstance(threshold, dict):
        for key in _AREA_BUDGET_KEYS.get(area, ()):
            value = threshold.get(key)
            if isinstance(value, (int, float)) and not isinstance(value, bool):
                return float(value)
        numerics = [
            float(v) for v in threshold.values()
            if isinstance(v, (int, float)) and not isinstance(v, bool)
        ]
        if numerics:
            return max(numerics)
    raise ValueError(f"non-numeric threshold for area {area!r}: {threshold!r}")


def load_thresholds(path):
    """Load gating thresholds from a budgets manifest file.

    Returns a dict mapping area name -> ``{"subsystem": str,
    "budget": float}``. Raises (fail closed) when the file is missing,
    unparseable, or lacks a ``budgets`` table.
    """
    with open(path, "r", encoding="utf-8") as handle:
        doc = json.load(handle)
    if not isinstance(doc, dict) or not isinstance(doc.get("budgets"), list):
        raise ValueError(f"malformed budgets manifest: {path!r}")
    thresholds = {}
    for row in doc["budgets"]:
        if not isinstance(row, dict):
            raise ValueError(f"malformed budget row in manifest: {path!r}")
        try:
            area = row["area"]
            subsystem = row["subsystem"]
            budget = _scalar_budget(area, row["threshold"])
        except KeyError as exc:
            raise ValueError(
                f"budget row missing {exc} in manifest: {path!r}"
            ) from exc
        current = thresholds.get(area)
        # Several rows can share an area (one per reference app); the gate
        # enforces the strictest budget so no app regresses silently.
        if current is None or budget < current["budget"]:
            thresholds[area] = {"subsystem": subsystem, "budget": budget}
    return thresholds
