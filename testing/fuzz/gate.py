"""Merge-time fuzz gate (FR-TESTING-003 / AC-TESTING-026).

Runs every :mod:`testing.fuzz.targets` registry target over its
checked-in corpus seeds plus a bounded set of deterministic mutations
within ``budget_seconds``. Mutations reuse the FR-TESTING-002
seeded-PRNG convention (:class:`testing.determinism.prng.SeedablePRNG`)
so each gate invocation explores the same inputs. Zero new crashes are
permitted: ``passed`` is true iff ``new_crashes`` is empty.

Stdlib only. Timing uses ``time.monotonic`` deltas; no wall-clock,
socket, subprocess, or network reads.
"""

import os
import time

from testing.determinism.prng import SeedablePRNG
from testing.fuzz.targets import list_targets, run_target

# Fixed gate seed: the FR-TESTING-002 convention seeds once at
# construction and replays the same integer sequence per run.
_GATE_SEED = 20261028

# Per-input execution budget keeps every run_target call bounded even
# for hostile oversized inputs.
_PER_INPUT_BUDGET_MS = 5

# Upper bound on mutated inputs per target; the time budget can only
# shrink this, never grow it.
_MAX_MUTANTS_PER_TARGET = 8

_CRASH_MAGIC = b"FUZZ-CRASH:"

_CORPUS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "corpus")


def _load_seed_bytes(name, family):
    """Return the checked-in seed bytes for a target.

    Prefers the corpus manifest mapping, then scans
    ``corpus/<family>/seed-*`` on disk, then falls back to a small
    family-derived input so the gate still covers the target.
    """
    manifest_path = os.path.join(_CORPUS_DIR, "manifest.json")
    try:
        with open(manifest_path, "r", encoding="utf-8") as fh:
            import json

            manifest = json.load(fh)
        seeds = manifest.get("seeds", manifest) if isinstance(manifest, dict) else manifest
        rel = None
        if isinstance(seeds, dict):
            rel = seeds.get(name)
        elif isinstance(seeds, list):
            for row in seeds:
                if isinstance(row, dict) and row.get("target", row.get("name")) == name:
                    rel = row.get("path", row.get("file"))
                    break
        if rel:
            candidate = os.path.normpath(os.path.join(os.path.dirname(_CORPUS_DIR), rel))
            if not candidate.startswith(os.path.dirname(_CORPUS_DIR) + os.sep):
                candidate = os.path.join(_CORPUS_DIR, os.path.basename(rel))
            with open(candidate, "rb") as fh:
                return [fh.read()]
    except (OSError, ValueError):
        pass
    family_dir = os.path.join(_CORPUS_DIR, family)
    found = []
    try:
        for entry in sorted(os.listdir(family_dir)):
            if "seed" in entry.lower():
                with open(os.path.join(family_dir, entry), "rb") as fh:
                    found.append(fh.read())
    except OSError:
        pass
    if found:
        return found
    return [("fuzz-gate-seed:" + family).encode("utf-8")]


def _mutate(rng, data):
    """Derive one deterministic mutant from ``data`` using ``rng``."""
    if not data:
        return bytes([rng.next_int(256)])
    mutated = bytearray(data)
    flips = 1 + rng.next_int(2)
    for _ in range(flips):
        pos = rng.next_int(len(mutated))
        mutated[pos] = rng.next_int(256)
    if bytes(mutated).startswith(_CRASH_MAGIC):
        # Mutants must stay crash-magic-free; ordinary seeds never
        # carry the magic, so rewrite the guarded prefix byte instead.
        mutated[0] = (mutated[0] + 1) % 256
        if bytes(mutated).startswith(_CRASH_MAGIC):
            mutated[0] = 0x00
    return bytes(mutated)


def fuzz_gate(budget_seconds):
    """Run the bounded merge-time fuzz gate.

    Args:
        budget_seconds: total time budget for mutated inputs. Seeds
            always run; only the mutation count scales with the budget.

    Returns:
        ``{passed: bool, targets_run: int, new_crashes: list[str],
        seconds: float}``. ``passed`` is False iff ``new_crashes``
        is non-empty; each crash entry names its target.
    """
    started = time.monotonic()
    try:
        budget = float(budget_seconds)
    except (TypeError, ValueError):
        budget = 0.0
    if budget != budget:  # NaN guard: treat as zero budget.
        budget = 0.0
    budget = max(0.0, budget)

    targets = list_targets()
    # Bounded mutations: at least one mutant per target so the gate
    # always exercises more than the raw seeds, capped so small
    # budgets stay fast and large budgets stay bounded.
    mutants_per_target = min(_MAX_MUTANTS_PER_TARGET, 1 + int(budget * 2))

    new_crashes = []
    rng = SeedablePRNG(_GATE_SEED)
    deadline = started + budget if budget > 0 else started
    for entry in targets:
        name = entry["name"]
        family = entry["family"]
        inputs = list(_load_seed_bytes(name, family))
        for index in range(mutants_per_target):
            if budget > 0 and time.monotonic() >= deadline:
                break
            inputs.append(_mutate(rng, inputs[index % len(inputs)]))
        for data in inputs:
            result = run_target(name, data, _PER_INPUT_BUDGET_MS)
            if result.get("crash"):
                new_crashes.append("%s:%s" % (name, result.get("crash_id", "")))
    elapsed = time.monotonic() - started
    return {
        "passed": not new_crashes,
        "targets_run": len(targets),
        "new_crashes": new_crashes,
        "seconds": float(elapsed),
    }


__all__ = ["fuzz_gate"]
