#!/usr/bin/env python3
"""Six-check admission aggregator (FR-DEPS-001, AC-DEPS-001).

Admits a manifest entry only when all six checks pass; any failing check
denies (exit 1, never warn/pass). Missing/unparseable manifests fail closed
via DepsDataError. Stdlib only.

ASSUMPTION — expected evidence key names (checks 1-4; checks 5-6 read the
existing ``licence`` and ``owner`` keys):
1. ``size_delta_bytes`` — per-dep measured delta record mirroring T2's
   ``size_delta_bytes=<N>`` + same-commit PR line (numeric).
2. ``dynamic_init_clean`` — per-dep dynamic-initializer verdict mirroring
   T1's pass/fail (must be True).
3. ``fno_exceptions`` — per-dep flag that the dep builds under
   ``-fno-exceptions`` where used on the render path, per CORE-001
   (must be True).
4. ``version`` — per-dep pinned version record (non-empty string).
Real ``build/dependency-manifest.json`` entries carry only
{name, owner, interface, exit_plan, licence, link}, so checks 1-4 deny
those entries fail-closed until those fields exist.

Check-name strings (one stable name per check, used in both the return
value and the CLI output): ``size-delta``, ``dynamic-init``,
``fno-exceptions``, ``pinned-version``, ``licence``, ``owner``.
"""

import argparse
import io
import json
import os
import sys
import tempfile
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

try:
    from tools.deps.licence_check import classify_licence as _sibling_classify
except Exception:
    _sibling_classify = None

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

CHECK_SIZE_DELTA = "size-delta"
CHECK_DYNAMIC_INIT = "dynamic-init"
CHECK_FNO_EXCEPTIONS = "fno-exceptions"
CHECK_PINNED_VERSION = "pinned-version"
CHECK_LICENCE = "licence"
CHECK_OWNER = "owner"


def _label(entry):
    if isinstance(entry, dict):
        name = entry.get("name")
        if isinstance(name, str) and name.strip():
            return name
    return repr(entry)


def _fail_size_delta(entry):
    value = entry.get("size_delta_bytes") if isinstance(entry, dict) else None
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return True
    return False


def _fail_dynamic_init(entry):
    value = entry.get("dynamic_init_clean") if isinstance(entry, dict) else None
    return value is not True


def _fail_fno_exceptions(entry):
    value = entry.get("fno_exceptions") if isinstance(entry, dict) else None
    return value is not True


def _fail_pinned_version(entry):
    value = entry.get("version") if isinstance(entry, dict) else None
    if not isinstance(value, str) or not value.strip():
        return True
    return False


def _fail_licence(entry):
    licence = entry.get("licence") if isinstance(entry, dict) else None
    if _sibling_classify is not None:
        allowed, _ = _sibling_classify(licence)
        return not allowed
    if not isinstance(licence, str) or not licence.strip():
        return True
    low = licence.strip().lower()
    denied_markers = ("gpl", "agpl", "sspl", "proprietar", "non-commercial",
                      "noncommercial", "field-of-use", "field of use")
    for marker in denied_markers:
        if marker in low and not (marker == "gpl" and "lgpl" in low):
            return True
    allowed_ids = ("mit", "apache-2.0", "bsd-2-clause", "bsd-3-clause", "isc",
                   "zlib", "unicode", "ftl", "ofl", "cc0", "public-domain")
    norm = "-".join(low.replace("_", " ").split())
    if norm in allowed_ids or "lgpl" in low:
        return False
    return True


def _fail_owner(entry):
    value = entry.get("owner") if isinstance(entry, dict) else None
    if not isinstance(value, str) or not value.strip():
        return True
    return False


def find_failures(entries):
    """Return [(dep_name, failed_check)] for entries failing any check."""
    failures = []
    for entry in entries:
        label = _label(entry)
        if _fail_size_delta(entry):
            failures.append((label, CHECK_SIZE_DELTA))
        if _fail_dynamic_init(entry):
            failures.append((label, CHECK_DYNAMIC_INIT))
        if _fail_fno_exceptions(entry):
            failures.append((label, CHECK_FNO_EXCEPTIONS))
        if _fail_pinned_version(entry):
            failures.append((label, CHECK_PINNED_VERSION))
        if _fail_licence(entry):
            failures.append((label, CHECK_LICENCE))
        if _fail_owner(entry):
            failures.append((label, CHECK_OWNER))
    return failures


def check_manifest(path):
    """Check *path*; return [(dep_name, failed_check)] (raises DepsDataError)."""
    entries = load_manifest(path)
    return find_failures(entries)


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

    def good_entry(name, **overrides):
        data = {
            "name": name,
            "owner": "core",
            "interface": "TestInterface",
            "exit_plan": "replace with local implementation",
            "licence": "MIT",
            "link": "static",
            "size_delta_bytes": 120,
            "dynamic_init_clean": True,
            "fno_exceptions": True,
            "version": "1.2.3",
        }
        data.update(overrides)
        return data

    def run_cli(manifest_path):
        buf = io.StringIO()
        with redirect_stdout(buf):
            code = main(["--manifest", manifest_path])
        return code, buf.getvalue()

    # Case 1: all-pass admits.
    path = tmp_manifest({"dependencies": [good_entry("good-a"), good_entry("good-b")]})
    try:
        try:
            result = check_manifest(path)
        except DepsDataError as exc:
            failures.append("all-pass raised: %s" % exc)
        else:
            if result:
                failures.append("all-pass reported failures: %r" % (result,))
            else:
                code, out = run_cli(path)
                if code == 0 and "admission_check: ok:" in out:
                    print("self-test: all-pass admits ok")
                else:
                    failures.append("all-pass CLI wrong: code=%r out=%r" % (code, out))
    finally:
        os.unlink(path)

    # Case 2: one failing fixture per check.
    single_failures = [
        ("size-delta", {"size_delta_bytes": None}, CHECK_SIZE_DELTA),
        ("dynamic-init", {"dynamic_init_clean": False}, CHECK_DYNAMIC_INIT),
        ("fno-exceptions", {"fno_exceptions": False}, CHECK_FNO_EXCEPTIONS),
        ("pinned-version", {"version": ""}, CHECK_PINNED_VERSION),
        ("licence", {"licence": "GPL-3.0"}, CHECK_LICENCE),
        ("owner", {"owner": ""}, CHECK_OWNER),
    ]
    for title, override, check_name in single_failures:
        dep = "dep-fail-%s" % title
        overrides = dict(override)
        if override.get("size_delta_bytes") is None and "size_delta_bytes" in override:
            bad = good_entry(dep)
            del bad["size_delta_bytes"]
        else:
            bad = good_entry(dep, **overrides)
        path = tmp_manifest([bad])
        try:
            try:
                result = check_manifest(path)
            except DepsDataError as exc:
                failures.append("%s fixture raised: %s" % (title, exc))
                continue
            names = [c for _, c in result]
            if check_name not in names:
                failures.append("%s fixture missing check %r: %r" % (title, check_name, result))
                continue
            if len(result) != 1:
                failures.append("%s fixture not isolated: %r" % (title, result))
                continue
            code, out = run_cli(path)
            expected = "admission_check: deny: %s failed %s" % (dep, check_name)
            if code != 1 or expected not in out or "FR-DEPS-001" not in out:
                failures.append("%s CLI wrong: code=%r out=%r" % (title, code, out))
            else:
                print("self-test: %s fails ok" % title)
        finally:
            os.unlink(path)

    # Case 3: missing manifest fails closed.
    try:
        check_manifest(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
    except DepsDataError:
        print("self-test: missing manifest fails ok")
    else:
        failures.append("missing manifest did not raise DepsDataError")
    code, _ = run_cli(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
    if code != 1:
        failures.append("missing manifest CLI did not exit 1")
    # Case 4: unparseable manifests fail closed.
    bad_payloads = [
        "{ this is not json !!!",
        json.dumps({"nope": []}),
        json.dumps({"dependencies": "not-a-list"}),
    ]
    for index, payload in enumerate(bad_payloads):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write(payload)
        tmp.close()
        try:
            try:
                check_manifest(tmp.name)
            except DepsDataError:
                print("self-test: unparseable manifest %d fails ok" % index)
            else:
                failures.append("unparseable manifest %d did not raise DepsDataError" % index)
            code, _ = run_cli(tmp.name)
            if code != 1:
                failures.append("unparseable manifest %d CLI did not exit 1" % index)
        finally:
            os.unlink(tmp.name)

    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Enforce six-check dependency admission.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        failures = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("admission_check: deny: %s (FR-DEPS-001)" % exc)
        return 1
    if failures:
        for name, check in failures:
            print(
                "admission_check: deny: %s failed %s (FR-DEPS-001)" % (name, check)
            )
        return 1
    print(
        "admission_check: ok: all %s manifest entries pass all six checks (FR-DEPS-001)"
        % len(load_manifest(args.manifest))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
