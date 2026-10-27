#!/usr/bin/env python3
"""Ownership/manifest check (FR-DEPS-004, AC-DEPS-009/010).

Validates that every ``load_manifest()`` entry names a non-empty owner,
isolating interface, and exit plan. Any gap fails closed. Missing or
unparseable manifests also fail closed (exit 1, never assumed permissive).
Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, get_allowlist, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

REQUIRED_FIELDS = ("owner", "interface", "exit_plan")


def _is_empty(value):
    """Return True when a required field value counts as missing."""
    if not isinstance(value, str):
        return True
    return not value.strip()


def find_gaps(entries):
    """Return ``[(entry_name, [missing fields])]`` for entries with gaps."""
    gaps = []
    for entry in entries:
        if isinstance(entry, dict):
            label = entry.get("name") if entry.get("name") else repr(entry)
        else:
            label = repr(entry)
        missing = []
        for field in REQUIRED_FIELDS:
            value = entry.get(field) if isinstance(entry, dict) else None
            if value is None or _is_empty(value):
                missing.append(field)
        if missing:
            gaps.append((label, missing))
    return gaps


def check_manifest(path):
    """Check *path*; return gap tuples (raises DepsDataError)."""
    entries = load_manifest(path)
    return find_gaps(entries)


def format_gap(entry_name, missing):
    return (
        "manifest_check: deny: dependency %r missing %s"
        % (entry_name, ", ".join(missing))
    )


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

    def entry(name, **overrides):
        data = {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": "MIT",
            "link": "static",
        }
        data.update(overrides)
        return data

    # Case 1: full manifest passes.
    names = [item["name"] for item in get_allowlist()][:2]
    path = tmp_manifest([entry(n) for n in names])
    try:
        try:
            gaps = check_manifest(path)
        except DepsDataError as exc:
            failures.append("full-manifest pass raised: %s" % exc)
        else:
            if gaps:
                failures.append("full-manifest pass reported gaps: %r" % (gaps,))
            else:
                print("self-test: full-manifest pass ok")
    finally:
        os.unlink(path)

    # Case 2: missing owner fails.
    bad = dict(entry("Hermes"))
    del bad["owner"]
    path = tmp_manifest([bad])
    try:
        try:
            gaps = check_manifest(path)
        except DepsDataError as exc:
            failures.append("missing owner raised instead of flagging: %s" % exc)
        else:
            if gaps and any(
                "hermes" in str(n).lower()
                and "owner" in [f.lower() for f in m]
                for n, m in gaps
            ):
                print("self-test: missing owner fails ok")
            else:
                failures.append("missing owner not flagged: %r" % (gaps,))
    finally:
        os.unlink(path)

    # Case 3: missing interface fails.
    bad = dict(entry("Yoga"))
    del bad["interface"]
    path = tmp_manifest([bad])
    try:
        try:
            gaps = check_manifest(path)
        except DepsDataError as exc:
            failures.append("missing interface raised instead of flagging: %s" % exc)
        else:
            if gaps and any(
                "yoga" in str(n).lower()
                and "interface" in [f.lower() for f in m]
                for n, m in gaps
            ):
                print("self-test: missing interface fails ok")
            else:
                failures.append("missing interface not flagged: %r" % (gaps,))
    finally:
        os.unlink(path)

    # Case 4: missing exit plan fails.
    bad = dict(entry("FlatBuffers"))
    del bad["exit_plan"]
    path = tmp_manifest([bad])
    try:
        try:
            gaps = check_manifest(path)
        except DepsDataError as exc:
            failures.append("missing exit plan raised instead of flagging: %s" % exc)
        else:
            if gaps and any(
                "flatbuffers" in str(n).lower()
                and "exit_plan" in [f.lower() for f in m]
                for n, m in gaps
            ):
                print("self-test: missing exit plan fails ok")
            else:
                failures.append("missing exit plan not flagged: %r" % (gaps,))
    finally:
        os.unlink(path)

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Enforce dependency ownership metadata.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        gaps = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("manifest_check: deny: %s; no ownership metadata covers this input" % exc)
        return 1
    if gaps:
        for entry_name, missing in gaps:
            print(format_gap(entry_name, missing))
        return 1
    print("manifest_check: ok: all %s manifest entries name owner, interface, exit_plan" % len(load_manifest(args.manifest)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
