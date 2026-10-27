#!/usr/bin/env python3
"""LGPL conditional-admission check (FR-DEPS-006, AC-DEPS-014).

Every manifest entry whose ``licence`` contains ``LGPL``
(case-insensitive) must prove ``link == dynamic`` and record the three
artifacts ``licence_text``, ``source_offer`` and ``relinkable_objects``.
Manifests with no LGPL entries pass vacuously. Missing/unparseable
manifests fail closed (exit 1). Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

REQUIRED_ARTIFACTS = ("licence_text", "source_offer", "relinkable_objects")


def is_lgpl(entry):
    """Return True when the entry's licence names LGPL (case-insensitive)."""
    licence = entry.get("licence") if isinstance(entry, dict) else None
    return isinstance(licence, str) and "lgpl" in licence.lower()


def find_violations(entries):
    """Return [(name, reason)] for LGPL entries failing admission."""
    violations = []
    for entry in entries:
        if not is_lgpl(entry):
            continue
        name = entry.get("name") if isinstance(entry, dict) else None
        label = name if name else repr(entry)
        link = entry.get("link") if isinstance(entry, dict) else None
        if not isinstance(link, str) or link.lower() != "dynamic":
            violations.append(
                (label, "LGPL link must be dynamic (got %r)" % (link,))
            )
            continue
        for field in REQUIRED_ARTIFACTS:
            value = entry.get(field) if isinstance(entry, dict) else None
            if not isinstance(value, str) or not value.strip():
                violations.append(
                    (label, "LGPL entry missing artifact %r" % (field,))
                )
    return violations


def check_manifest(path):
    """Check *path*; return LGPL violations (raises DepsDataError)."""
    return find_violations(load_manifest(path))


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

    def lgpl(name):
        return entry(
            name,
            licence="LGPL-2.1-or-later",
            link="dynamic",
            licence_text="third-party/%s-LICENSE (LGPL text)" % name,
            source_offer="exact sources including local patches offered per LGPL section 6",
            relinkable_objects="%s-relinkable-objects.zip recorded in SBOM" % name,
        )

    # Case 1: non-LGPL manifest passes vacuously.
    path = tmp_manifest([entry("st-a"), entry("st-b", licence="Apache-2.0")])
    try:
        try:
            violations = check_manifest(path)
        except DepsDataError as exc:
            failures.append("non-LGPL pass raised: %s" % exc)
        else:
            if violations:
                failures.append("non-LGPL pass reported violations: %r" % (violations,))
            else:
                print("self-test: non-LGPL pass ok")
    finally:
        os.unlink(path)

    # Case 2: compliant LGPL entry passes.
    path = tmp_manifest([lgpl("st-lgpl-ok-xyz")])
    try:
        try:
            violations = check_manifest(path)
        except DepsDataError as exc:
            failures.append("LGPL pass raised: %s" % exc)
        else:
            if violations:
                failures.append("LGPL pass reported violations: %r" % (violations,))
            else:
                print("self-test: LGPL dynamic with artifacts ok")
    finally:
        os.unlink(path)

    # Case 3: static LGPL fails.
    bad = lgpl("st-lgpl-static-xyz")
    bad["link"] = "static"
    path = tmp_manifest([bad])
    try:
        try:
            violations = check_manifest(path)
        except DepsDataError as exc:
            failures.append("static LGPL case raised: %s" % exc)
        else:
            if violations and violations[0][0] == "st-lgpl-static-xyz":
                print("self-test: static LGPL fails ok")
            else:
                failures.append("static LGPL not flagged: %r" % (violations,))
    finally:
        os.unlink(path)

    # Case 4: missing manifest fails closed.
    try:
        check_manifest(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
    except DepsDataError:
        print("self-test: missing manifest fails ok")
    else:
        failures.append("missing manifest did not raise DepsDataError")

    # Case 5: unparseable manifest fails closed.
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
    parser = argparse.ArgumentParser(description="Enforce LGPL conditional admission.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        violations = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("lgpl_check: deny: %s; LGPL admission unproven" % exc)
        return 1
    if violations:
        for label, reason in violations:
            print("lgpl_check: deny: LGPL dependency %r: %s" % (label, reason))
        return 1
    print(
        "lgpl_check: ok: all LGPL entries in %s link dynamically with recorded artifacts"
        % len(load_manifest(args.manifest))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
