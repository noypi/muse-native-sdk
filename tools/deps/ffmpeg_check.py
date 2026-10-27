#!/usr/bin/env python3
"""FFmpeg LGPL-configuration check (FR-DEPS-007, AC-DEPS-017/018).

Every manifest entry whose ``name`` contains ``ffmpeg``
(case-insensitive) must prove shared-library linkage
(``link`` of ``shared`` or ``dynamic``) and record its ``configure``
line; the configure line must not carry ``--enable-gpl``,
``--enable-nonfree``, or GPL-only codecs (``libx264``/``libx265``
family). Manifests with no FFmpeg entries pass vacuously.
Missing/unparseable manifests and missing configure lines fail
closed (exit 1). Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

LGPL_CONFIGURE = "--enable-shared --disable-static --disable-gpl --disable-nonfree"


def is_ffmpeg(entry):
    """Return True when the entry's name contains ffmpeg (case-insensitive)."""
    name = entry.get("name") if isinstance(entry, dict) else None
    return isinstance(name, str) and "ffmpeg" in name.lower()


def check_entry(entry):
    """Return a violation reason for an FFmpeg entry, or None when admitted."""
    name = entry.get("name") if isinstance(entry, dict) else None
    label = name if name else repr(entry)
    link = entry.get("link") if isinstance(entry, dict) else None
    if not isinstance(link, str) or link.lower() not in ("shared", "dynamic"):
        return (label, "FFmpeg link must be shared-library (got %r)" % (link,))
    configure = entry.get("configure") if isinstance(entry, dict) else None
    if not isinstance(configure, str) or not configure.strip():
        return (label, "FFmpeg entry missing recorded configure line")
    low = configure.lower()
    if "--enable-gpl" in low:
        return (label, "FFmpeg configure refuses GPL flag --enable-gpl")
    if "--enable-nonfree" in low:
        return (label, "FFmpeg configure refuses nonfree flag --enable-nonfree")
    if "libx264" in low:
        return (label, "FFmpeg configure refuses GPL-only codec libx264")
    if "libx265" in low:
        return (label, "FFmpeg configure refuses GPL-only codec libx265")
    return None


def find_violations(entries):
    """Return [(name, reason)] for FFmpeg entries failing admission."""
    violations = []
    for entry in entries:
        if not is_ffmpeg(entry):
            continue
        violation = check_entry(entry)
        if violation is not None:
            violations.append(violation)
    return violations


def check_manifest(path):
    """Check *path*; return FFmpeg violations (raises DepsDataError)."""
    return find_violations(load_manifest(path))


def _tmp_manifest(entries):
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(entries, tmp)
    tmp.close()
    return tmp.name


def _ffmpeg_entry(**overrides):
    data = {
        "name": "FFmpeg",
        "owner": "media",
        "interface": "CodecModule",
        "exit_plan": "replace decoders with platform MediaCodec",
        "licence": "LGPL-2.1-or-later",
        "link": "dynamic",
        "configure": LGPL_CONFIGURE,
    }
    data.update(overrides)
    return data


def run_self_test():
    """Self-check that an LGPL shared-library FFmpeg entry passes; 0/1."""
    failures = []
    path = _tmp_manifest([_ffmpeg_entry()])
    try:
        try:
            violations = check_manifest(path)
        except DepsDataError as exc:
            failures.append("LGPL-shared pass raised: %s" % exc)
        else:
            if violations:
                failures.append("LGPL-shared pass reported violations: %r" % (violations,))
            else:
                print("self-test: LGPL shared-library FFmpeg ok")
    finally:
        os.unlink(path)
    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def run_self_test_gpl():
    """Self-check that an --enable-gpl FFmpeg entry is refused; 0/1."""
    failures = []
    path = _tmp_manifest(
        [_ffmpeg_entry(configure=LGPL_CONFIGURE + " --enable-gpl")]
    )
    try:
        try:
            violations = check_manifest(path)
        except DepsDataError as exc:
            failures.append("GPL fixture raised: %s" % exc)
        else:
            if violations and "--enable-gpl" in str(violations):
                print("self-test-gpl: --enable-gpl refused ok")
            else:
                failures.append("--enable-gpl fixture not refused: %r" % (violations,))
    finally:
        os.unlink(path)
    for failure in failures:
        print("self-test failure: %s" % failure)
    return 1 if failures else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description="Enforce FFmpeg LGPL configuration.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--self-test-gpl", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()
    if args.self_test_gpl:
        return run_self_test_gpl()

    try:
        violations = check_manifest(args.manifest)
    except DepsDataError as exc:
        print("ffmpeg_check: deny: %s; FFmpeg LGPL configuration unproven" % exc)
        return 1
    if violations:
        for label, reason in violations:
            print("ffmpeg_check: deny: FFmpeg dependency %r: %s" % (label, reason))
        return 1
    print(
        "ffmpeg_check: ok: all FFmpeg entries in %s are shared-library LGPL with recorded configure"
        % len(load_manifest(args.manifest))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
