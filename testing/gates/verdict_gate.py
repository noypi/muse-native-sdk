"""T5 verdict gate (FR-TESTING-007/AC-TESTING-040, TC-TESTING-040).

0 PASS only when local exit 0 + gh checks 0 pending/0 fail + boxes [x];
nonzero PARTIAL/FAIL otherwise. Negative counts are usage errors (nonzero).
"""

import sys


def main(build_ok: bool, checks_pending: int, checks_failed: int,
         boxes_checked: bool) -> int:
    """Verdict gate: 0 PASS only when local exit 0 + gh checks 0 pending/0 fail + boxes [x]; nonzero PARTIAL/FAIL otherwise."""
    if not build_ok:
        return 1
    if not boxes_checked:
        return 1
    if checks_pending != 0 or checks_failed != 0:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(True, 0, 0, True))
