#!/usr/bin/env python3
"""Production-link gate (FR-DEPS-008, AC-DEPS-019; FR-TESTING-005 production side).

No hermetic test double may be reachable from production: every manifest
entry must be on the allow-list, and no entry may look like a mock/fake/
stub test double. Missing/unparseable manifests fail closed (DepsDataError,
never assumed permissive). Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, get_allowlist, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

# Case-insensitive name fragments marking a hermetic test double.
_DOUBLE_SUBSTRINGS = ("mock", "fake", "stub", "double")


def is_double_like(name):
    """Return True when *name* looks like a hermetic test double."""
    lowered = name.lower()
    return any(fragment in lowered for fragment in _DOUBLE_SUBSTRINGS)


def check_production_links(manifest_path):
    """Check *manifest_path*; return violation strings (raises DepsDataError)."""
    entries = load_manifest(manifest_path)
    allowed = {item["name"] for item in get_allowlist()}
    violations = []
    for entry in entries:
        name = entry.get("name") if isinstance(entry, dict) else None
        label = name if name else repr(entry)
        if not name or name not in allowed:
            violations.append(
                "production_link_check: deny: dependency %r is not on the "
                "allow-list (missing allow-list entry)" % (label,)
            )
        elif is_double_like(name):
            violations.append(
                "production_link_check: deny: dependency %r looks like a "
                "hermetic test double linked into production" % (name,)
            )
    # A non-allow-listed double is already reported above; also flag the
    # double nature explicitly so the reason is actionable.
    for entry in entries:
        name = entry.get("name") if isinstance(entry, dict) else None
        if name and name not in allowed and is_double_like(name):
            violations.append(
                "production_link_check: deny: dependency %r looks like a "
                "hermetic test double linked into production" % (name,)
            )
    return violations


def run_self_test():
    """Run hermetic self-checks; return 0 on success, 1 on failure."""
    failures = []

    def tmp_manifest(entries):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        json.dump(entries, tmp)
        tmp.close()
        return tmp.name

    def entry(name):
        return {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": "MIT",
            "link": "static",
        }

    # Case 1: clean manifest passes.
    names = [item["name"] for item in get_allowlist()][:2]
    path = tmp_manifest([entry(n) for n in names])
    try:
        try:
            violations = check_production_links(path)
        except DepsDataError as exc:
            failures.append("clean pass raised: %s" % exc)
        else:
            if violations:
                failures.append("clean pass reported violations: %r" % (violations,))
            else:
                print("self-test: clean pass ok")
    finally:
        os.unlink(path)

    # Case 2: unknown dep fails naming it.
    bad = "evil-lib-not-allowlisted-xyz"
    path = tmp_manifest([entry(bad)])
    try:
        try:
            violations = check_production_links(path)
        except DepsDataError as exc:
            failures.append("unknown dep raised instead of flagging: %s" % exc)
        else:
            if any(bad in v for v in violations):
                print("self-test: unknown dep fails ok")
            else:
                failures.append("unknown dep not flagged: %r" % (violations,))
    finally:
        os.unlink(path)

    # Case 3: mock-named entry fails naming it.
    bad = "MockHermes"
    path = tmp_manifest([entry(bad)])
    try:
        try:
            violations = check_production_links(path)
        except DepsDataError as exc:
            failures.append("mock entry raised instead of flagging: %s" % exc)
        else:
            if any(bad in v for v in violations):
                print("self-test: mock entry fails ok")
            else:
                failures.append("mock entry not flagged: %r" % (violations,))
    finally:
        os.unlink(path)

    # Case 4: double-symbol entry fails naming it.
    bad = "fake-hermes-for-tests"
    path = tmp_manifest([entry(bad)])
    try:
        try:
            violations = check_production_links(path)
        except DepsDataError as exc:
            failures.append("double-symbol entry raised instead of flagging: %s" % exc)
        else:
            if any(bad in v for v in violations):
                print("self-test: double-symbol entry fails ok")
            else:
                failures.append("double-symbol entry not flagged: %r" % (violations,))
    finally:
        os.unlink(path)

    # Case 5: missing manifest fails closed.
    try:
        check_production_links(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
    except DepsDataError:
        print("self-test: missing manifest fails ok")
    else:
        failures.append("missing manifest did not raise DepsDataError")

    # Case 6: unparseable manifest fails closed.
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    tmp.write("{ this is not json !!!")
    tmp.close()
    try:
        try:
            check_production_links(tmp.name)
        except DepsDataError:
            print("self-test: unparseable manifest fails ok")
        else:
            failures.append("unparseable manifest did not raise DepsDataError")
    finally:
        os.unlink(tmp.name)

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Gate production links against test doubles.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        violations = check_production_links(args.manifest)
    except DepsDataError as exc:
        print("production_link_check: deny: %s; no allow-list entry covers this input" % exc)
        return 1
    if violations:
        for violation in violations:
            print(violation)
        return 1
    print("production_link_check: ok: no test double reachable from %s production entries"
          % len(load_manifest(args.manifest)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
