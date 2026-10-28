"""Crash-to-regression promotion (FR-TESTING-003 / AC-TESTING-024).

Promotes a crashing input into a checked-in regression case under
``testing/fuzz/regressions/<family>/<crash_id>.bin`` plus one manifest
row mapping crash_id to fix-status. Promoting the same crash twice is
idempotent. Unknown families are rejected with ``ValueError``.

Stdlib only. No wall-clock reads, no randomness, no network, no
subprocess, no device.
"""

import json
import os
import re

from testing.fuzz.targets import list_targets

_BASE_DIR = os.path.dirname(os.path.abspath(__file__))
_REGRESSIONS_DIR = os.path.join(_BASE_DIR, "regressions")
_MANIFEST_PATH = os.path.join(_REGRESSIONS_DIR, "manifest.json")

# Crash ids become file names, so only a conservative charset is
# accepted. Anything with a separator, traversal segment, or shell
# metacharacter is rejected with ValueError and never touches disk.
_SAFE_CRASH_ID = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]*\Z")

# Fix-status values for the manifest row.
_STATUS_OPEN = "open"


def _families():
    """Return the set of known AC-TESTING-023 family names."""
    return {entry["family"] for entry in list_targets()}


def _check_family(family):
    if family not in _families():
        raise ValueError("unknown fuzz family: %r" % (family,))


def _check_crash_id(crash_id):
    if (
        not isinstance(crash_id, str)
        or not _SAFE_CRASH_ID.match(crash_id)
        or crash_id in (".", "..")
    ):
        raise ValueError("unsafe crash id: %r" % (crash_id,))


def _test_name(family, crash_id):
    """Derive a stable, non-empty test name for a regression case."""
    safe_family = re.sub(r"[^A-Za-z0-9_]", "_", family)
    safe_crash = re.sub(r"[^A-Za-z0-9_]", "_", crash_id)
    return "test_regression_%s_%s" % (safe_family, safe_crash)


def _relative_path(family, crash_id):
    return "testing/fuzz/regressions/%s/%s.bin" % (family, crash_id)


def _load_manifest():
    """Return the manifest row list; missing/corrupt reads as empty."""
    try:
        with open(_MANIFEST_PATH, "r", encoding="utf-8") as fh:
            manifest = json.load(fh)
    except (OSError, ValueError):
        return []
    rows = manifest.get("regressions", manifest) if isinstance(manifest, dict) else manifest
    if not isinstance(rows, list):
        return []
    return [row for row in rows if isinstance(row, dict)]


def _save_manifest(rows):
    os.makedirs(_REGRESSIONS_DIR, exist_ok=True)
    tmp_path = _MANIFEST_PATH + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as fh:
        json.dump({"regressions": rows}, fh, indent=2, sort_keys=True)
        fh.write("\n")
    os.replace(tmp_path, _MANIFEST_PATH)


def promote_crash(family, crash_bytes, crash_id):
    """Promote a crashing input to a checked-in regression case.

    Writes ``crash_bytes`` to
    ``testing/fuzz/regressions/<family>/<crash_id>.bin`` and records
    one manifest row mapping crash_id to fix-status. Re-promoting the
    same crash returns the same result without duplicating the file or
    the manifest row. Raises ``ValueError`` on unknown families and on
    hostile crash ids; oversized inputs are stored as-is, never raise.
    """
    _check_family(family)
    _check_crash_id(crash_id)
    raw = bytes(crash_bytes)

    family_dir = os.path.join(_REGRESSIONS_DIR, family)
    os.makedirs(family_dir, exist_ok=True)
    full_path = os.path.join(family_dir, crash_id + ".bin")
    try:
        with open(full_path, "rb") as fh:
            existing = fh.read()
    except OSError:
        existing = None
    if existing != raw:
        with open(full_path, "wb") as fh:
            fh.write(raw)

    rows = _load_manifest()
    if not any(row.get("crash_id") == crash_id for row in rows):
        rows.append(
            {
                "crash_id": crash_id,
                "family": family,
                "status": _STATUS_OPEN,
            }
        )
        _save_manifest(rows)

    return {
        "regression_path": _relative_path(family, crash_id),
        "test_name": _test_name(family, crash_id),
    }


def list_regressions(family):
    """Return sorted regression paths for ``family``.

    Raises ``ValueError`` on unknown families.
    """
    _check_family(family)
    paths = set()
    for row in _load_manifest():
        if row.get("family") == family and isinstance(row.get("crash_id"), str):
            try:
                _check_crash_id(row["crash_id"])
            except ValueError:
                continue
            paths.add(_relative_path(family, row["crash_id"]))
    family_dir = os.path.join(_REGRESSIONS_DIR, family)
    try:
        entries = os.listdir(family_dir)
    except OSError:
        entries = []
    for entry in entries:
        if entry.endswith(".bin"):
            stem = entry[: -len(".bin")]
            try:
                _check_crash_id(stem)
            except ValueError:
                continue
            paths.add(_relative_path(family, stem))
    return sorted(paths)
