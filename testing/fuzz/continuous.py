"""Continuous fuzz job on the sanitizer build (FR-TESTING-003 / AC-TESTING-025).

Maps every :mod:`testing.fuzz.targets` registry target to a sanitizer
schedule entry owned by CORE-MSAFETY and routes filed findings to the
owning alias while auto-creating the T3 regression case via
:mod:`testing.fuzz.regress`. Sanitizer values (``asan``/``ubsan``/``msan``)
are labels consumed here — no toolchain work happens in this module.

Crash-id note: ``run_target`` crash ids are SHA-256 hashes of
family+bytes, so the original crashing bytes are not recoverable from a
bare ``crash_id`` in general. :func:`file_finding` therefore tries the
harness's own deterministic probe shapes for the family and keeps the
first one whose ``run_target`` rerun reproduces the exact ``crash_id``;
otherwise it stores representative crashing bytes (magic-prefixed, so the
stored case still flags as a crash) under the given ``crash_id`` without
claiming exact reproduction.

Stdlib only. No wall-clock reads, no randomness, no network, no
subprocess, no device.
"""

from testing.fuzz.regress import promote_crash
from testing.fuzz.targets import get_target, list_targets, run_target

# Sanitizer variants owned by CORE-MSAFETY; consumed as labels only.
_KNOWN_SANITIZERS = ("asan", "ubsan", "msan")

# Crash magic shared with testing.fuzz.targets: inputs starting with
# this prefix flag run_target as a crash.
_CRASH_MAGIC = b"FUZZ-CRASH:"

# Bounded budget for the best-effort reproduction check in file_finding.
_VERIFY_BUDGET_MS = 5


def _owner_alias_for_family(family):
    """Return the deterministic owning alias for a fuzz family."""
    return "fuzz-%s-owners" % (family,)


def _corpus_for_family(family):
    """Return the seed-corpus path consumed by a family's schedule entry."""
    return "testing/fuzz/corpus/%s" % (family,)


def _target_name_for_family(family):
    """Return the first registry target name for ``family``.

    Raises ``ValueError`` on unknown families, mirroring the T3
    ``regress`` convention.
    """
    for entry in list_targets():
        if entry["family"] == family:
            return entry["name"]
    raise ValueError("unknown fuzz family: %r" % (family,))


def _representative_crash_bytes(target_name, family, crash_id):
    """Return bytes to store for a finding, preferring exact reproduction.

    Tries the harness's deterministic probe shapes for ``family`` and
    returns the first whose ``run_target`` rerun yields ``crash_id``.
    Falls back to representative magic-prefixed bytes naming the crash
    id (still flagged as a crash by ``run_target``) when no probe
    reproduces it — an exact preimage of the hash is infeasible.
    """
    probes = (
        _CRASH_MAGIC + b"t4-repro-" + family.encode("utf-8"),
        _CRASH_MAGIC + family.encode("utf-8"),
    )
    for probe in probes:
        try:
            observed = run_target(target_name, probe, _VERIFY_BUDGET_MS)
        except (KeyError, ValueError):
            continue
        if observed.get("crash_id") == crash_id:
            return probe
    return _CRASH_MAGIC + family.encode("utf-8") + b":" + str(crash_id).encode("utf-8")


def schedule_entry(target):
    """Return the continuous-fuzz schedule entry for ``target``.

    Returns ``{target: str, sanitizer: str, owner_alias: str,
    corpus: str}``. Raises ``KeyError`` on unknown targets.
    """
    entry = get_target(target)  # KeyError on unknown names.
    sanitizer = entry["sanitizer"]
    if sanitizer not in _KNOWN_SANITIZERS:
        raise ValueError("unknown sanitizer variant: %r" % (sanitizer,))
    return {
        "target": entry["name"],
        "sanitizer": sanitizer,
        "owner_alias": _owner_alias_for_family(entry["family"]),
        "corpus": _corpus_for_family(entry["family"]),
    }


def file_finding(owner_alias, crash_id, family):
    """File a fuzz finding against its owning alias.

    Auto-creates the T3 regression case by delegating storage to
    :func:`testing.fuzz.regress.promote_crash`. Returns ``{finding_id:
    str, owner_alias: str, regression_path: str}``. Raises
    ``ValueError`` on empty owner aliases, unknown families, and
    hostile crash ids (via the ``promote_crash`` validation).
    """
    if not isinstance(owner_alias, str) or not owner_alias:
        raise ValueError("owner_alias must be a non-empty string")
    target_name = _target_name_for_family(family)
    crash_bytes = _representative_crash_bytes(target_name, family, crash_id)
    # promote_crash validates the family and crash id (ValueError on
    # unknown families / hostile ids) and records the manifest row.
    promoted = promote_crash(family, crash_bytes, crash_id)
    return {
        "finding_id": "finding-%s-%s" % (family, crash_id),
        "owner_alias": owner_alias,
        "regression_path": promoted["regression_path"],
    }


__all__ = ["file_finding", "schedule_entry"]
