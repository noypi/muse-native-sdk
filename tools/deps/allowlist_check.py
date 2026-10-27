#!/usr/bin/env python3
"""Allow-list enforcement check (FR-DEPS-002, AC-DEPS-004/006).

Compares ``load_manifest()`` entry names against ``get_allowlist()`` from
:mod:`tools.deps._deps_data`; any manifest entry not on the allow-list
fails closed. Missing/unparseable manifests also fail closed (exit 1,
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


def find_unknown(entries, allowed):
    """Return manifest entry names not present on the allow-list."""
    unknown = []
    for entry in entries:
        name = entry.get("name") if isinstance(entry, dict) else None
        if not name or name not in allowed:
            unknown.append(name if name else repr(entry))
    return unknown


def check_manifest(path):
    """Check *path*; return unknown-dep names (raises DepsDataError)."""
    entries = load_manifest(path)
    allowed = {item["name"] for item in get_allowlist()}
    return find_unknown(entries, allowed)


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
            unknown = check_manifest(path)
        except DepsDataError as exc:
            failures.append("clean pass raised: %s" % exc)
        else:
            if unknown:
                failures.append("clean pass reported unknown: %r" % (unknown,))
            else:
                print("self-test: clean pass ok")
    finally:
        os.unlink(path)

    # Case 2: unknown dep fails.
    bad = "evil-lib-not-allowlisted-xyz"
    path = tmp_manifest([entry(bad)])
    try:
        try:
            unknown = check_manifest(path)
        except DepsDataError as exc:
            failures.append("unknown dep raised instead of flagging: %s" % exc)
        else:
            if bad in unknown:
                print("self-test: unknown dep fails ok")
            else:
                failures.append("unknown dep not flagged: %r" % (unknown,))
    finally:
        os.unlink(path)

    # Case 3: missing manifest fails closed.
    try:
        check_manifest(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
    except DepsDataError:
        print("self-test: missing manifest fails ok")
    else:
        failures.append("missing manifest did not raise DepsDataError")

    # Case 4: unparseable manifest fails closed.
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    tmp.write("{ this is not json !!!")
    tmp.close()
    try:
        try:
            check_manifest(tmp.name)
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
    parser = argparse.ArgumentParser(description="Enforce the dependency allow-list.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        unknown = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("allowlist_check: deny: %s; no allow-list entry covers this input" % exc)
        return 1
    if unknown:
        for name in unknown:
            print(
                "allowlist_check: deny: dependency %r is not on the allow-list "
                "(missing allow-list entry)" % (name,)
            )
        return 1
    print("allowlist_check: ok: all %s manifest entries are allow-listed" % len(load_manifest(args.manifest)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
