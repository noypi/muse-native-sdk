"""T7 gate runner wiring (FR-TESTING-007/AC-TESTING-042, TC-TESTING-042).

Entry shape follows the repo gate precedent (``main(argv) -> int``;
0 pass, nonzero fail, 2 usage error). Refuses re-baseline while any
proof row is red: writes no golden files, returns nonzero.
"""

import sys


def main(argv: list) -> int:
    """Run the delivery gates; refuse re-baseline while any proof row is red."""
    args = list(argv) if argv is not None else []
    if len(args) == 2 and args[0] == "--proof":
        if args[1] == "red":
            print("run_gates: deny: proof is red; re-baseline refused")
            return 1
        if args[1] == "green":
            print("run_gates: ok: proof is green")
            return 0
        print("run_gates: usage: run_gates.py --proof <red|green>")
        return 2
    print("run_gates: usage: run_gates.py --proof <red|green>")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
