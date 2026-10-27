#!/usr/bin/env python3
"""Licence allow-list enforcement check (FR-DEPS-006, AC-DEPS-013/016).

Compares ``load_manifest()`` entry ``licence`` fields against the closed
allow-list (MIT, Apache-2.0, BSD-2/3, ISC, zlib, Unicode, FTL, OFL, CC0)
versus the denied families (GPL, AGPL, SSPL, non-commercial, field-of-use,
proprietary without terms). Missing or unidentifiable licences are denied,
never assumed permissive. LGPL entries are allowed here; their dynamic-link
plus artifact conditions are enforced by ``lgpl_check.py`` (AC-DEPS-014).
Missing/unparseable manifests fail closed (exit 1). Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

# Case-insensitive substrings marking an allow-listed licence.
_ALLOW_SUBSTRINGS = (
    "mit",
    "apache",
    "bsd",
    "isc",
    "zlib",
    "unicode",
    "ftl",
    "ofl",
    "cc0",
    "public domain",
    "lgpl",
)


def classify_licence(licence):
    """Return (allowed, reason) for a raw ``licence`` field value."""
    if not isinstance(licence, str) or not licence.strip():
        return False, "missing licence (treated as denied)"
    low = licence.lower()
    # Denied families first (LGPL must not trip the GPL rule).
    if "lgpl" in low:
        return True, "LGPL (conditional admission via lgpl_check)"
    if "agpl" in low:
        return False, "AGPL denied"
    if "sspl" in low:
        return False, "SSPL denied"
    if "gpl" in low:
        return False, "GPL denied"
    if (
        "non-commercial" in low
        or "noncommercial" in low
        or "non commercial" in low
        or "cc-by-nc" in low
    ):
        return False, "non-commercial terms denied"
    if (
        "field-of-use" in low
        or "field of use" in low
        or "field_of_use" in low
    ):
        return False, "field-of-use restriction denied"
    if "proprietar" in low:
        return False, "proprietary licence without terms denied"
    for token in _ALLOW_SUBSTRINGS:
        if token in low:
            return True, "allow-listed (%s)" % token
    return False, "unidentifiable licence (treated as denied)"


def find_denied(entries):
    """Return [(name, licence, reason)] for entries failing licence policy."""
    denied = []
    for entry in entries:
        name = entry.get("name") if isinstance(entry, dict) else None
        label = name if name else repr(entry)
        licence = entry.get("licence") if isinstance(entry, dict) else None
        allowed, reason = classify_licence(licence)
        if not allowed:
            denied.append((label, licence, reason))
    return denied


def check_manifest(path):
    """Check *path*; return denied entries (raises DepsDataError)."""
    return find_denied(load_manifest(path))


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

    def entry(name, licence="MIT"):
        return {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": licence,
            "link": "static",
        }

    # Case 1: clean manifest passes.
    clean = [
        entry("st-a", "MIT"),
        entry("st-b", "Apache-2.0"),
        entry("st-c", "BSD-3-Clause"),
        entry("st-d", "ISC"),
    ]
    path = tmp_manifest(clean)
    try:
        try:
            denied = check_manifest(path)
        except DepsDataError as exc:
            failures.append("clean pass raised: %s" % exc)
        else:
            if denied:
                failures.append("clean pass reported denied: %r" % (denied,))
            else:
                print("self-test: clean pass ok")
    finally:
        os.unlink(path)

    # Case 2: GPL denied.
    path = tmp_manifest([entry("st-gpl-xyz", "GPL-3.0")])
    try:
        try:
            denied = check_manifest(path)
        except DepsDataError as exc:
            failures.append("GPL case raised: %s" % exc)
        else:
            if denied and denied[0][0] == "st-gpl-xyz":
                print("self-test: GPL denied ok")
            else:
                failures.append("GPL licence not denied: %r" % (denied,))
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


def run_self_test_unidentified():
    """Self-check that unidentifiable licences are denied; 0/1."""
    failures = []

    def tmp_manifest(entries):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        json.dump(entries, tmp)
        tmp.close()
        return tmp.name

    def entry(name, licence="MIT"):
        return {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": licence,
            "link": "static",
        }

    path = tmp_manifest([entry("st-weird-xyz", "Totally-Custom-1.0-xyz")])
    try:
        try:
            denied = check_manifest(path)
        except DepsDataError as exc:
            failures.append("unidentified case raised: %s" % exc)
        else:
            if denied and denied[0][0] == "st-weird-xyz":
                print("self-test-unidentified: unidentified licence denied ok")
            else:
                failures.append(
                    "unidentified licence not denied: %r" % (denied,)
                )
    finally:
        os.unlink(path)

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Enforce the licence allow-list.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--self-test-unidentified", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test_unidentified:
        return run_self_test_unidentified()
    if args.self_test:
        return run_self_test()

    try:
        denied = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("licence_check: deny: %s; no licence covers this input" % exc)
        return 1
    if denied:
        for label, licence, reason in denied:
            print(
                "licence_check: deny: dependency %r licence %r: %s"
                % (label, licence, reason)
            )
        return 1
    print(
        "licence_check: ok: all %s manifest entries carry allow-listed licences"
        % len(load_manifest(args.manifest))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
