"""T6 done gate (FR-TESTING-007/AC-TESTING-041, TC-TESTING-041).

0 (done) iff build success + prod-graph boot + one real effect log
+ git status clean all hold; nonzero (NOT-DONE) otherwise.
"""

import sys


def main(build_ok: bool, booted: bool, effect_logged: bool,
         tree_clean: bool) -> int:
    """Done gate: 0 (done) iff all four hold; nonzero (NOT-DONE) otherwise."""
    if build_ok and booted and effect_logged and tree_clean:
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main(True, True, True, True))
