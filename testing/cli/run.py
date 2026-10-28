"""Single-CLI four-tier orchestrator (FR-TESTING-001 / AC-TESTING-004).

Runs the C++ unit, JS unit, snapshot, and integration tiers from one
invocation and leaves one JUnit XML file per tier under ``output_dir``.

Stdlib only. No wall-clock reads, no live I/O, no device required. The
T2/T3 tier modules may not exist yet when this module is imported, so
each tier entry point is imported lazily and guarded: a missing tier
counts as a failing tier, never an import crash.
"""

import os
import sys
from xml.sax.saxutils import escape

TIERS = ("cpp", "js", "snapshot", "integration")

try:  # T2 tier (may not exist yet when T1 lands).
    from testing.cli.cpp_unit import run_cpp_unit
except Exception:  # ImportError and friends: failing tier, not crash.

    def run_cpp_unit(suite_filter=""):  # type: ignore[misc]
        """Fallback when the C++ tier module is absent."""
        raise ModuleNotFoundError("testing.cli.cpp_unit is not available")


try:  # T2 tier (may not exist yet when T1 lands).
    from testing.cli.js_unit import run_js_unit
except Exception:  # ImportError and friends: failing tier, not crash.

    def run_js_unit(suite_filter=""):  # type: ignore[misc]
        """Fallback when the JS tier module is absent."""
        raise ModuleNotFoundError("testing.cli.js_unit is not available")


try:  # T3 tier (may not exist yet when T1 lands).
    from testing.cli.snapshot import render_snapshot
except Exception:  # ImportError and friends: failing tier, not crash.

    def render_snapshot(component="", variant=""):  # type: ignore[misc]
        """Fallback when the snapshot tier module is absent."""
        raise ModuleNotFoundError("testing.cli.snapshot is not available")


try:  # T3 tier (may not exist yet when T1 lands).
    from testing.cli.integration import drive_integration
except Exception:  # ImportError and friends: failing tier, not crash.

    def drive_integration(scenario=""):  # type: ignore[misc]
        """Fallback when the integration tier module is absent."""
        raise ModuleNotFoundError("testing.cli.integration is not available")


def _write_junit_xml(path, tier_name, tests, failures):
    """Write a minimal parseable JUnit XML file for one tier."""
    cases = []
    for index in range(tests):
        failed = index < failures
        if failed:
            cases.append(
                '  <testcase classname="%s" name="case-%d">'
                '<failure message="tier failure">failed</failure>'
                "</testcase>" % (escape(tier_name), index)
            )
        else:
            cases.append(
                '  <testcase classname="%s" name="case-%d"/>' % (escape(tier_name), index)
            )
    document = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<testsuite name="%s" tests="%d" failures="%d" '
        'errors="0" skipped="0">\n%s\n</testsuite>\n'
        % (escape(tier_name), tests, failures, "\n".join(cases))
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(document)
    return path


def _failing_tier(output_dir, tier_name, reason):
    """Synthesize a failing TierResult for a tier that raised."""
    del reason  # The JUnit XML carries the gate signal, not the message.
    junit_xml = os.path.join(output_dir, "%s.xml" % tier_name)
    _write_junit_xml(junit_xml, tier_name, 1, 1)
    return {"passed": False, "tests": 1, "failures": 1, "junit_xml": junit_xml}


def _tier_from_counts(output_dir, tier_name, tests, failures):
    """Build a TierResult from tier counts, writing its JUnit XML."""
    tests = int(tests)
    failures = int(failures)
    if tests < 0:
        tests = 0
    if failures < 0:
        failures = 0
    if failures > tests:
        failures = tests
    junit_xml = os.path.join(output_dir, "%s.xml" % tier_name)
    _write_junit_xml(junit_xml, tier_name, tests, failures)
    return {
        "passed": failures == 0,
        "tests": tests,
        "failures": failures,
        "junit_xml": junit_xml,
    }


def _run_cpp_tier(output_dir):
    try:
        result = run_cpp_unit("")
        return _tier_from_counts(
            output_dir, "cpp", result["tests"], result["failures"]
        )
    except Exception as exc:
        return _failing_tier(output_dir, "cpp", exc)


def _run_js_tier(output_dir):
    try:
        result = run_js_unit("")
        return _tier_from_counts(output_dir, "js", result["tests"], result["failures"])
    except Exception as exc:
        return _failing_tier(output_dir, "js", exc)


def _run_snapshot_tier(output_dir):
    try:
        result = render_snapshot("Button", "light")
        failures = 0 if result.get("passed", False) else 1
        return _tier_from_counts(output_dir, "snapshot", 1, failures)
    except Exception as exc:
        return _failing_tier(output_dir, "snapshot", exc)


def _run_integration_tier(output_dir):
    try:
        result = drive_integration("launch")
        steps = int(result.get("steps", 1))
        if steps < 1:
            steps = 1
        failures = 0 if result.get("passed", False) else steps
        return _tier_from_counts(output_dir, "integration", steps, failures)
    except Exception as exc:
        return _failing_tier(output_dir, "integration", exc)


def run_all_tiers(output_dir: str) -> dict[str, dict]:
    """Run all four tiers and return ``{tier: TierResult}``.

    Writes one JUnit XML file per tier under ``output_dir`` (created
    when missing) and prints a one-line-per-tier summary. A tier whose
    entry point is missing or raises counts as a failing tier.
    """
    os.makedirs(output_dir, exist_ok=True)
    runners = (
        ("cpp", _run_cpp_tier),
        ("js", _run_js_tier),
        ("snapshot", _run_snapshot_tier),
        ("integration", _run_integration_tier),
    )
    results = {}
    for tier_name, runner in runners:
        tier_result = runner(output_dir)
        results[tier_name] = tier_result
        print(
            "tier %s: %s tests=%d failures=%d xml=%s"
            % (
                tier_name,
                "PASS" if tier_result["passed"] else "FAIL",
                tier_result["tests"],
                tier_result["failures"],
                tier_result["junit_xml"],
            )
        )
    return results


def main(argv):
    """CLI entry point: exit 0 iff every tier passed, 1 otherwise."""
    output_dir = argv[1] if len(argv) > 1 else os.path.join("build", "testing", "tiers")
    results = run_all_tiers(output_dir)
    passed = all(tier["passed"] for tier in results.values())
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
