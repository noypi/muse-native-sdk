"""Explicit per-test golden tolerance store (FR-TESTING-004 / AC-TESTING-034).

Stdlib only. No wall-clock reads, no network, no device.
Every golden test id (``<component>-<variant>`` per
``coverage._golden_path``) has an explicit entry in ``tolerances.json``;
there is no global default — a missing entry raises ``KeyError``.
``set_tolerance`` requires a non-empty review ref and appends the change
to the ``audit`` log inside ``tolerances.json``.
"""

import json
import os

TOLERANCE_KEYS = ("mean_per_channel_error", "max_differing_pixels")

_TOLERANCES_PATH = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "tolerances.json"
)

__all__ = ["get_tolerance", "set_tolerance"]


def _load_raw(path=_TOLERANCES_PATH):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _entries(raw):
    if isinstance(raw, dict) and isinstance(raw.get("tolerances"), dict):
        return raw["tolerances"]
    if isinstance(raw, dict):
        return {
            key: value
            for key, value in raw.items()
            if isinstance(value, dict) and "audit" not in key
        }
    return {}


def _check_test_id_shape(test_id):
    """Fail closed on traversal escapes; empty/non-string is KeyError."""
    if not isinstance(test_id, str) or not test_id:
        raise KeyError("unknown golden test id: %r" % (test_id,))
    if (
        "/" in test_id
        or "\\" in test_id
        or test_id in (".", "..")
        or ".." in test_id.split("/")
        or ".." in test_id.split("\\")
    ):
        raise ValueError("invalid golden test id: %r" % (test_id,))
    return test_id


def _check_tolerance(tolerance):
    if not isinstance(tolerance, dict):
        raise TypeError("tolerance must be a dict")
    for key in TOLERANCE_KEYS:
        if key not in tolerance:
            raise ValueError("tolerance missing %r" % (key,))
    mean = tolerance["mean_per_channel_error"]
    pixels = tolerance["max_differing_pixels"]
    if isinstance(mean, bool) or not isinstance(mean, (int, float)):
        raise TypeError("mean_per_channel_error must be numeric")
    if isinstance(pixels, bool) or not isinstance(pixels, int):
        raise TypeError("max_differing_pixels must be an int")
    if mean < 0:
        raise ValueError("mean_per_channel_error must be >= 0")
    if pixels < 0:
        raise ValueError("max_differing_pixels must be >= 0")
    return {"mean_per_channel_error": float(mean), "max_differing_pixels": pixels}


def get_tolerance(test_id: str) -> dict:
    """Return the explicit tolerance for ``test_id``.

    Raises ``KeyError`` when ``test_id`` has no explicit entry — no
    silent global default is applied.
    """
    if not isinstance(test_id, str) or not test_id:
        raise KeyError("unknown golden test id: %r" % (test_id,))
    if (
        "/" in test_id
        or "\\" in test_id
        or ".." in test_id.split("/")
        or ".." in test_id.split("\\")
    ):
        raise KeyError("unknown golden test id: %r" % (test_id,))
    entries = _entries(_load_raw())
    if test_id not in entries:
        raise KeyError("unknown golden test id: %r" % (test_id,))
    entry = entries[test_id]
    return _check_tolerance(entry)


def set_tolerance(test_id: str, tolerance: dict, review_ref: str) -> dict:
    """Store ``tolerance`` for ``test_id`` under review gate ``review_ref``.

    Raises ``PermissionError`` when ``review_ref`` is empty (including
    ``None``) without mutating ``tolerances.json``; traversal test ids
    are rejected with ``ValueError``. On success the change is appended
    to the ``audit`` log and the stored entry is returned.
    """
    if not isinstance(review_ref, str) or not review_ref:
        raise PermissionError("tolerance change requires a review ref")
    _check_test_id_shape(test_id)
    clean = _check_tolerance(tolerance)
    path = _TOLERANCES_PATH
    with open(path, "r", encoding="utf-8") as handle:
        raw = json.load(handle)
    if isinstance(raw, dict) and isinstance(raw.get("tolerances"), dict):
        tolerances = raw["tolerances"]
        audit = raw.get("audit")
        if not isinstance(audit, list):
            audit = []
            raw["audit"] = audit
    elif isinstance(raw, dict):
        tolerances = {
            key: value
            for key, value in raw.items()
            if isinstance(value, dict) and "audit" not in key
        }
        audit = raw.get("audit")
        if not isinstance(audit, list):
            audit = []
        raw = {"tolerances": tolerances, "audit": audit}
        tolerances = raw["tolerances"]
    else:
        raise ValueError("tolerances.json has an unsupported shape")
    tolerances[test_id] = dict(clean)
    audit.append(
        {
            "test_id": test_id,
            "tolerance": dict(clean),
            "review_ref": review_ref,
        }
    )
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(raw, handle, indent=2, sort_keys=True)
        handle.write("\n")
    return dict(clean)
