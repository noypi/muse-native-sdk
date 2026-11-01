#!/usr/bin/env python3
"""Staging/rollback gate (FR-DEPS-008, AC-DEPS-020).

Enforces net -> bundler -> codecs staging order on manifest entries
carrying an ``area`` field, and requires one independent (non-empty,
distinct) rollback flag per staged area from a JSON flags file of the
form ``{area: flag}``. Missing/unparseable inputs fail closed via
:exc:`DepsDataError`. Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")
DEFAULT_FLAGS = os.path.join("build", "rollback-flags.json")

STAGE_ORDER = ("net", "bundler", "codecs")
_STAGE_INDEX = {area: index for index, area in enumerate(STAGE_ORDER)}


def load_flags(path):
    """Load the flags file, failing closed (raises DepsDataError)."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except FileNotFoundError as exc:
        raise DepsDataError("flags not found: %s" % path) from exc
    except (OSError, ValueError) as exc:
        raise DepsDataError("flags unparseable: %s (%s)" % (path, exc)) from exc
    if not isinstance(data, dict):
        raise DepsDataError(
            "flags unparseable: %s (expected an object mapping area to flag)" % path
        )
    return data


def check_staging(manifest_path, flags_path):
    """Check staging order and rollback flags; return violation strings.

    Raises :exc:`DepsDataError` on missing/unparseable input.
    """
    entries = load_manifest(manifest_path)
    flags = load_flags(flags_path)

    observed = []
    for entry in entries:
        area = entry.get("area") if isinstance(entry, dict) else None
        if area in _STAGE_INDEX:
            observed.append(area)

    required = [area for area in STAGE_ORDER if area in observed]

    violations = []

    for area in required:
        flag = flags.get(area)
        if not isinstance(flag, str) or not flag:
            violations.append(
                "staging_rollback_check: deny: area '%s' lacks an "
                "independent rollback flag (missing or empty)" % area
            )

    seen = {}
    for area in required:
        flag = flags.get(area)
        if isinstance(flag, str) and flag:
            seen.setdefault(flag, []).append(area)
    for flag, areas in seen.items():
        if len(areas) > 1:
            violations.append(
                "staging_rollback_check: deny: areas %s share rollback "
                "flag %r (flags must be independent)"
                % (", ".join("'%s'" % a for a in areas), flag)
            )

    last_index = -1
    for area in observed:
        index = _STAGE_INDEX[area]
        if index < last_index:
            violations.append(
                "staging_rollback_check: deny: staging order violated: "
                "area '%s' staged out of net -> bundler -> codecs order" % area
            )
            break
        last_index = index

    return violations


def run_self_test():
    """Run hermetic self-checks; return 0 on success, 1 on failure."""
    failures = []

    def tmp_json(payload):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        json.dump(payload, tmp)
        tmp.close()
        return tmp.name

    def staged_entry(name, area):
        return {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": "MIT",
            "link": "static",
            "area": area,
        }

    def full_flags():
        return {
            "net": "rollback-net-xyz",
            "bundler": "rollback-bundler-xyz",
            "codecs": "rollback-codecs-xyz",
        }

    def ordered_manifest():
        return [
            staged_entry("Cronet", "net"),
            staged_entry("Play Core NDK", "bundler"),
            staged_entry("Media3", "codecs"),
        ]

    # Case 1: ordered with flags passes.
    manifest = tmp_json(ordered_manifest())
    flags = tmp_json(full_flags())
    try:
        try:
            violations = check_staging(manifest, flags)
        except DepsDataError as exc:
            failures.append("clean pass raised: %s" % exc)
        else:
            if violations:
                failures.append("clean pass reported violations: %r" % (violations,))
            else:
                print("self-test: clean pass ok")
    finally:
        os.unlink(manifest)
        os.unlink(flags)

    # Case 2: missing area flag flagged.
    manifest = tmp_json(ordered_manifest())
    flagset = full_flags()
    del flagset["codecs"]
    flags = tmp_json(flagset)
    try:
        try:
            violations = check_staging(manifest, flags)
        except DepsDataError as exc:
            failures.append("missing flag raised instead of flagging: %s" % exc)
        else:
            if violations and any("codecs" in v for v in violations):
                print("self-test: missing flag fails ok")
            else:
                failures.append("missing flag not flagged: %r" % (violations,))
    finally:
        os.unlink(manifest)
        os.unlink(flags)

    # Case 3: coupled flag flagged.
    manifest = tmp_json(ordered_manifest())
    flagset = full_flags()
    flagset["bundler"] = flagset["net"]
    flags = tmp_json(flagset)
    try:
        try:
            violations = check_staging(manifest, flags)
        except DepsDataError as exc:
            failures.append("coupled flag raised instead of flagging: %s" % exc)
        else:
            if violations:
                print("self-test: coupled flag fails ok")
            else:
                failures.append("coupled flag not flagged: %r" % (violations,))
    finally:
        os.unlink(manifest)
        os.unlink(flags)

    # Case 4: order violation flagged.
    manifest = tmp_json(
        [
            staged_entry("Media3", "codecs"),
            staged_entry("Cronet", "net"),
            staged_entry("Play Core NDK", "bundler"),
        ]
    )
    flags = tmp_json(full_flags())
    try:
        try:
            violations = check_staging(manifest, flags)
        except DepsDataError as exc:
            failures.append("order violation raised instead of flagging: %s" % exc)
        else:
            if violations:
                print("self-test: order violation fails ok")
            else:
                failures.append("order violation not flagged: %r" % (violations,))
    finally:
        os.unlink(manifest)
        os.unlink(flags)

    # Case 5: missing manifest fails closed.
    flags = tmp_json(full_flags())
    try:
        try:
            check_staging(
                os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"), flags
            )
        except DepsDataError:
            print("self-test: missing manifest fails ok")
        else:
            failures.append("missing manifest did not raise DepsDataError")
    finally:
        os.unlink(flags)

    # Case 6: unparseable flags fails closed.
    manifest = tmp_json(ordered_manifest())
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    tmp.write("{ this is not json !!!")
    tmp.close()
    try:
        try:
            check_staging(manifest, tmp.name)
        except DepsDataError:
            print("self-test: unparseable flags fails ok")
        else:
            failures.append("unparseable flags did not raise DepsDataError")
    finally:
        os.unlink(manifest)
        os.unlink(tmp.name)

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Enforce staging/rollback gates.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--flags", default=DEFAULT_FLAGS)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        violations = check_staging(args.manifest, args.flags)
    except DepsDataError as exc:
        print("staging_rollback_check: deny: %s" % exc)
        return 1
    if violations:
        for violation in violations:
            print(violation)
        return 1
    print("staging_rollback_check: ok: staging order and rollback flags verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
