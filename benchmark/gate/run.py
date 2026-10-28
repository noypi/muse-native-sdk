"""CI perf gate: fail the build naming the subsystem on budget excess.

Compares measured ``results`` against the thresholds imported from
``benchmark/budgets.json`` (single edit point) and applies the repeat-run
noise-band/flake policy: only a regression that exceeds the noise band —
robustly aggregated across repeats so one spiky sample is tolerated — is
reported as a failure. Anything else (in-band jitter, a single spike, an
unknown area) passes.

CLI: ``python benchmark/gate/run.py <manifest> <results-json>`` where the
results file holds a JSON object of area -> number (or array of numbers for
repeat runs). Exit 0 on pass, nonzero naming each breaching subsystem.
"""

import json
import math
import os
import sys
from dataclasses import dataclass, field

# When executed as a script (``python benchmark/gate/run.py ...``) the
# interpreter puts ``benchmark/gate`` — not the repo root — on sys.path,
# so bootstrap the root for the ``benchmark.gate`` package import.
sys.path.insert(
    0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)

from benchmark.gate.thresholds import load_thresholds

# Relative noise band: excess within this fraction over budget is treated
# as measurement jitter, not a regression (AC-TESTING-031). Repeat-run
# aggregation uses the median so a single spike cannot fail the gate
# while a consistent excess across repeats does (AC-TESTING-029).
NOISE_BAND = 0.05


@dataclass
class Failure:
    subsystem: str
    area: str
    budget: float
    actual: float


@dataclass
class GateResult:
    passed: bool
    failures: list = field(default_factory=list)


def _aggregate(measured):
    """Robust aggregate of one area's measurements (median of repeats)."""
    if isinstance(measured, (list, tuple)):
        values = [float(v) for v in measured]
        if not values:
            raise ValueError("empty repeat-run sample list")
        for value in values:
            if not math.isfinite(value):
                raise ValueError("non-finite measurement rejected fail-closed")
        ordered = sorted(values)
        mid = len(ordered) // 2
        if len(ordered) % 2:
            return ordered[mid]
        return (ordered[mid - 1] + ordered[mid]) / 2.0
    value = float(measured)
    if not math.isfinite(value):
        raise ValueError("non-finite measurement rejected fail-closed")
    return value


def gate(manifest_path: str, results: dict) -> GateResult:
    """Gate ``results`` (area -> float or list-of-floats) against ``manifest_path``.

    Raises (fail closed) when the manifest is missing or malformed.
    Results naming an area absent from the manifest are ignored.
    """
    thresholds = load_thresholds(manifest_path)
    failures = []
    for area, measured in results.items():
        entry = thresholds.get(area)
        if entry is None:
            continue
        actual = _aggregate(measured)
        budget = entry["budget"]
        if actual > budget * (1.0 + NOISE_BAND):
            failures.append(
                Failure(
                    subsystem=entry["subsystem"],
                    area=area,
                    budget=budget,
                    actual=actual,
                )
            )
    return GateResult(passed=not failures, failures=failures)


def main(argv):
    if len(argv) != 3:
        print(f"usage: {argv[0]} <manifest> <results-json>", file=sys.stderr)
        return 2
    manifest_path, results_path = argv[1], argv[2]
    with open(results_path, "r", encoding="utf-8") as handle:
        results = json.load(handle)
    result = gate(manifest_path, results)
    if result.passed:
        print("perf gate passed")
        return 0
    for failure in result.failures:
        print(
            f"perf gate breach: subsystem {failure.subsystem} area "
            f"{failure.area} budget {failure.budget} actual {failure.actual}"
        )
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
