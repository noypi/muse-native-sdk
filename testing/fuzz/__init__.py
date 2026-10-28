"""Fuzz target inventory (FR-TESTING-003 / AC-TESTING-023).

Stdlib only. No wall-clock reads, no network, no device.
Sanitizer values are labels owned by CORE-MSAFETY (no toolchain work).
"""

from testing.fuzz.targets import get_target, list_targets, run_target

__all__ = ["get_target", "list_targets", "run_target"]
