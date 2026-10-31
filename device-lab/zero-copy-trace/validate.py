#!/usr/bin/env python3
"""zero-copy-trace evidence validator (NFR-PVIEWS-002 / AC-PVIEWS-006).

Evidence JSON: {"noMemcpy": bool, "zOrderOk": bool, "handlesLeaked": int}

Verdict: exit 0 iff noMemcpy is True AND zOrderOk is True AND
handlesLeaked == 0. Strict types: noMemcpy/zOrderOk must be JSON boolean
True exactly (True only; "true"/1/null/missing fail); handlesLeaked must be
a real int (bool is not int) equal to 0. Anything else exits 1.
Usage errors exit 2.

Hermetic self-test: `validate.py --self-test` runs in-process fixtures
(valid pass; each flag failing; wrong types; missing keys; malformed JSON;
1000-cycle leak count nonzero) with no device, no network, no adb.
"""
import json
import sys
import tempfile
import os


def validate_doc(doc):
    """Return (ok, reason). Strict AC-PVIEWS-006 verdict."""
    if not isinstance(doc, dict):
        return False, "top-level object required"
    if doc.get("noMemcpy") is not True:
        return False, "noMemcpy not boolean true (CPU memcpy present)"
    if doc.get("zOrderOk") is not True:
        return False, "zOrderOk not boolean true (z-order wrong under transform)"
    leaked = doc.get("handlesLeaked", None)
    if type(leaked) is not int:
        return False, "handlesLeaked must be int"
    if leaked != 0:
        return False, "handlesLeaked=%d (1000 attach/detach cycles leaked)" % leaked
    return True, "ok"


def validate_file(path):
    try:
        with open(path) as f:
            doc = json.load(f)
    except Exception as e:
        return False, "invalid evidence json at %s: %s" % (path, e)
    return validate_doc(doc)


def self_test():
    cases = [
        # (name, doc-or-raw, expect_ok)
        ("valid", {"noMemcpy": True, "zOrderOk": True, "handlesLeaked": 0}, True),
        ("memcpy-present", {"noMemcpy": False, "zOrderOk": True, "handlesLeaked": 0}, False),
        ("zorder-bad", {"noMemcpy": True, "zOrderOk": False, "handlesLeaked": 0}, False),
        ("handles-leaked", {"noMemcpy": True, "zOrderOk": True, "handlesLeaked": 3}, False),
        ("leak-1000-cycle-1", {"noMemcpy": True, "zOrderOk": True, "handlesLeaked": 1}, False),
        ("string-true", {"noMemcpy": "true", "zOrderOk": True, "handlesLeaked": 0}, False),
        ("int-1-flag", {"noMemcpy": 1, "zOrderOk": True, "handlesLeaked": 0}, False),
        ("bool-leaked", {"noMemcpy": True, "zOrderOk": True, "handlesLeaked": False}, False),
        ("string-leaked", {"noMemcpy": True, "zOrderOk": True, "handlesLeaked": "0"}, False),
        ("missing-noMemcpy", {"zOrderOk": True, "handlesLeaked": 0}, False),
        ("missing-zOrderOk", {"noMemcpy": True, "handlesLeaked": 0}, False),
        ("missing-handles", {"noMemcpy": True, "zOrderOk": True}, False),
        ("top-level-list", [1, 2], False),
        ("null-doc", None, False),
    ]
    failures = []
    for name, payload, expect_ok in cases:
        if isinstance(payload, str) and payload == "RAW-BAD-JSON":
            raw = "{not json"
        else:
            raw = json.dumps(payload)
        fd, tmp = tempfile.mkstemp(suffix=".json")
        try:
            with os.fdopen(fd, "w") as f:
                f.write(raw)
            ok, _ = validate_file(tmp)
        finally:
            os.unlink(tmp)
        if ok != expect_ok:
            failures.append("%s: want %s got %s" % (name, expect_ok, ok))
    # malformed raw JSON must fail
    fd, tmp = tempfile.mkstemp(suffix=".json")
    try:
        with os.fdopen(fd, "w") as f:
            f.write("{not json")
        ok, _ = validate_file(tmp)
    finally:
        os.unlink(tmp)
    if ok:
        failures.append("malformed-json: want False got True")
    # missing file must fail
    ok, _ = validate_file("/nonexistent-zero-copy-trace-$$.json")
    if ok:
        failures.append("missing-file: want False got True")
    if failures:
        for fl in failures:
            print("self-test FAIL: %s" % fl, file=sys.stderr)
        return 1
    print("self-test: %d cases passed" % (len(cases) + 2))
    return 0


def usage():
    print("usage: validate.py <evidence.json> | validate.py --self-test", file=sys.stderr)


def main(argv):
    if len(argv) != 2:
        usage()
        return 2
    if argv[1] in ("--self-test", "-t"):
        return self_test()
    if argv[1] in ("--help", "-h"):
        print("zero-copy-trace evidence validator (NFR-PVIEWS-002 / AC-PVIEWS-006)")
        print("usage: validate.py <evidence.json> | validate.py --self-test")
        print('evidence: {"noMemcpy": true, "zOrderOk": true, "handlesLeaked": 0}')
        return 0
    ok, reason = validate_file(argv[1])
    if not ok:
        print("validate.py: evidence at %s fails: %s" % (argv[1], reason), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
