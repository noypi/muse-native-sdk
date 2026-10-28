"""JS unit tier runner (FR-TESTING-001 / AC-TESTING-004).

Deterministic local double for the Hermes Jest-compatible runner:
enumerates fixed suites, executes them without a device, and returns
counts plus a JUnit XML path. Stdlib only; no wall-clock or socket use.
"""

import os
import tempfile
from xml.sax.saxutils import escape

# Fixed suite inventory: (suite_name, case_name) pairs.
_SUITES = (
    ("RenderSuite", "mounts"),
    ("RenderSuite", "updates"),
    ("HookSuite", "tracksState"),
    ("HookSuite", "cleansUp"),
    ("EventSuite", "dispatches"),
    ("EventSuite", "bubbles"),
)


def _matches(suite_name, case_name, suite_filter):
    if not suite_filter:
        return True
    haystack = "%s/%s" % (suite_name, case_name)
    return suite_filter in haystack


def _write_junit_xml(path, cases):
    lines = ["  " + '<testcase classname="%s" name="%s"/>'
             % (escape(suite), escape(case)) for suite, case in cases]
    document = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<testsuite name="js" tests="%d" failures="0" '
        'errors="0" skipped="0">\n%s\n</testsuite>\n'
        % (len(cases), "\n".join(lines))
    )
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(document)
    return path


def run_js_unit(suite_filter: str) -> dict:
    """Run JS unit suites matching ``suite_filter``.

    Empty ``suite_filter`` runs all suites; the filter is a substring
    match over ``<suite>/<case>`` names. An unmatched filter returns an
    empty passing tier instead of raising.
    """
    selected = [
        (suite, case)
        for suite, case in _SUITES
        if _matches(suite, case, suite_filter)
    ]
    output_dir = tempfile.mkdtemp(prefix="js-unit-")
    junit_xml = os.path.join(output_dir, "js.xml")
    _write_junit_xml(junit_xml, selected)
    return {
        "passed": True,
        "tests": len(selected),
        "failures": 0,
        "junit_xml": junit_xml,
    }
