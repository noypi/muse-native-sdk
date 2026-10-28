"""Integration tier double (FR-TESTING-001 / AC-TESTING-004).

Deterministic local double for the ADB protocol driver: replays a
scripted scenario step list from a virtual timeline and writes an
ordered event log. Stdlib only; no device, no wall-clock, no socket.
"""

import os
import tempfile

# Scripted scenario inventory: scenario name -> ordered step names.
_SCENARIOS = {
    "launch": ("start", "wait-for-idle", "assert-home", "stop"),
    "login": ("start", "enter-username", "enter-password", "submit", "stop"),
    "logout": ("start", "open-menu", "sign-out", "stop"),
}


def drive_integration(scenario: str) -> dict:
    """Replay ``scenario`` and write its ordered event log.

    Fail-closed: an unscripted ``scenario`` raises instead of returning
    a failing result.
    """
    if scenario not in _SCENARIOS:
        raise ValueError("unknown scenario: %r" % (scenario,))
    steps = _SCENARIOS[scenario]
    output_dir = tempfile.mkdtemp(prefix="integration-")
    log_path = os.path.join(output_dir, "%s.log" % scenario)
    with open(log_path, "w", encoding="utf-8") as handle:
        for index, step in enumerate(steps):
            handle.write("%d: %s\n" % (index, step))
    return {
        "passed": True,
        "steps": len(steps),
        "log_path": log_path,
    }
