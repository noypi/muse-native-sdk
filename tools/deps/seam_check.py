#!/usr/bin/env python3
"""Seam gate check (FR-DEPS-008, AC-DEPS-021).

Every isolating seam keeps its injection point: each ``load_manifest()``
entry must name a non-empty owner, a known isolating interface (seam),
and a non-empty exit plan. Entries naming an unknown seam fail. Missing
or unparseable manifests fail closed (exit 1, never assumed permissive).
Stdlib only.
"""

import argparse
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

# Known isolating seams: FR-DEPS-008 Notes seams expressed as manifest
# interface tokens (observed in build/dependency-manifest.json), plus
# ITransport (T2 records Cronet behind interface ITransport).
KNOWN_SEAMS = frozenset(
    {
        "AudioOut",
        "CodecModule",
        "CrashReport",
        "DebugJson",
        "Delivery",
        "DeviceSelect",
        "EnumDiag",
        "FastMap",
        "FormatUtil",
        "FramePacer",
        "GlyphRaster",
        "GpuAllocator",
        "ITransport",
        "JsRuntime",
        "LayoutEngine",
        "PayloadSchema",
        "Playback",
        "RealtimeTransport",
        "TextI18n",
        "TextShaper",
        "VectorImport",
        "VulkanApi",
        "WorkerQueue",
    }
)


def _is_empty(value):
    """Return True when a required field value counts as missing."""
    if not isinstance(value, str):
        return True
    return not value.strip()


def check_seams(manifest_path):
    """Check *manifest_path*; return violation strings (raises DepsDataError)."""
    entries = load_manifest(manifest_path)
    violations = []
    for entry in entries:
        name = entry.get("name") if isinstance(entry, dict) else None
        label = name if name else repr(entry)
        if not isinstance(entry, dict):
            violations.append(
                "seam_check: deny: dependency %r is not a manifest object "
                "(missing seam metadata)" % (label,)
            )
            continue
        for field in ("owner", "interface", "exit_plan"):
            if _is_empty(entry.get(field)):
                violations.append(
                    "seam_check: deny: dependency %r missing %s "
                    "(seam without injection point)" % (label, field)
                )
        interface = entry.get("interface")
        if (
            isinstance(interface, str)
            and interface.strip()
            and interface.strip() not in KNOWN_SEAMS
        ):
            violations.append(
                "seam_check: deny: dependency %r names unknown seam %r "
                "(no such isolating interface)" % (label, interface.strip())
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

    def entry(name, interface, owner="core"):
        return {
            "name": name,
            "owner": owner,
            "interface": interface,
            "exit_plan": "replace with local implementation",
            "licence": "MIT",
            "link": "static",
        }

    # Case 1: clean manifest passes.
    path = tmp_manifest(
        [entry("Hermes", "JsRuntime"), entry("Cronet", "ITransport", owner="net")]
    )
    try:
        try:
            violations = check_seams(path)
        except DepsDataError as exc:
            failures.append("clean pass raised: %s" % exc)
        else:
            if violations:
                failures.append("clean pass reported violations: %r" % (violations,))
            else:
                print("self-test: clean pass ok")
    finally:
        os.unlink(path)

    # Case 2: missing interface fails naming the entry.
    path = tmp_manifest([entry("Hermes", "")])
    try:
        try:
            violations = check_seams(path)
        except DepsDataError as exc:
            failures.append("missing interface raised instead of flagging: %s" % exc)
        else:
            if any("Hermes" in v for v in violations):
                print("self-test: missing interface fails ok")
            else:
                failures.append("missing interface not flagged: %r" % (violations,))
    finally:
        os.unlink(path)

    # Case 3: unknown seam fails naming the entry.
    path = tmp_manifest([entry("Hermes", "NotARealSeam-xyz")])
    try:
        try:
            violations = check_seams(path)
        except DepsDataError as exc:
            failures.append("unknown seam raised instead of flagging: %s" % exc)
        else:
            if any("Hermes" in v for v in violations):
                print("self-test: unknown seam fails ok")
            else:
                failures.append("unknown seam not flagged: %r" % (violations,))
    finally:
        os.unlink(path)

    # Case 4: missing manifest fails closed.
    try:
        check_seams(os.path.join(tempfile.gettempdir(), "does-not-exist-xyz.json"))
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
            check_seams(tmp.name)
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
    parser = argparse.ArgumentParser(description="Enforce isolating seams keep injection points.")
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    try:
        violations = check_seams(args.manifest)
    except DepsDataError as exc:
        print("seam_check: deny: %s; no seam metadata covers this input" % exc)
        return 1
    if violations:
        for violation in violations:
            print(violation)
        return 1
    print("seam_check: ok: all %s manifest entries keep their seams" % len(load_manifest(args.manifest)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
