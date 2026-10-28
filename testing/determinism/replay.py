"""Same-seed replay determinism check (FR-TESTING-002).

Runs the snapshot + integration path twice under the virtual timeline
with the seeded generator and byte-compares the outputs and the
event-ordering logs. Stdlib only; no live I/O.
"""

import hashlib


def _single_pass(seed: int, steps: list[str]) -> tuple[bytes, list[str]]:
    """Execute one deterministic pass; return (digest, event log)."""
    from testing.determinism.clock import VirtualClock, step_frames
    from testing.determinism.prng import SeedablePRNG, pinned_environment
    from testing.cli.integration import drive_integration
    from testing.cli.snapshot import render_snapshot

    with pinned_environment(seed):
        clock = VirtualClock()
        rng = SeedablePRNG(seed)
        order: list[str] = []

        def on_frame(frame: int) -> None:
            order.append("frame-%d@%d" % (frame, clock.now()))

        if steps:
            step_frames(clock, len(steps), on_frame)
        draws = [rng.next_int(2**31) for _ in steps]
        snap = render_snapshot("replay", "canary")
        with open(snap["png_path"], "rb") as handle:
            snap_bytes = handle.read()
        integ = drive_integration("launch")
        with open(integ["log_path"], "r", encoding="utf-8") as handle:
            integ_log = handle.read()
        step_log = [
            "%d@%s:%d" % (index, step, draw)
            for index, (step, draw) in enumerate(zip(steps, draws))
        ]
        log = list(order) + step_log + integ_log.splitlines()
        payload = repr(
            (seed, tuple(steps), tuple(draws), tuple(order),
             snap_bytes, integ_log)
        ).encode("utf-8")
        return hashlib.sha256(payload).digest(), log


def replay_twice(seed: int, steps: list[str]) -> dict:
    """Replay ``steps`` twice under ``seed``; compare both passes.

    Args:
        seed: Seed driving the virtual timeline and generator.
        steps: Ordered timeline step names (may be empty).

    Returns:
        Dict with ``byte_identical``/``logs_identical`` booleans and
        the echoed ``seed``.
    """
    first_digest, first_log = _single_pass(seed, list(steps))
    second_digest, second_log = _single_pass(seed, list(steps))
    return {
        "byte_identical": first_digest == second_digest,
        "logs_identical": first_log == second_log,
        "seed": seed,
    }


__all__ = ["replay_twice"]
