#!/usr/bin/env python3
"""Seam gate check (FR-DEPS-008, AC-DEPS-021; FR-DEPS-013 T6, AC-DEPS-049).

Every isolating seam keeps its injection point: each ``load_manifest()``
entry must name a non-empty owner, a known isolating interface (seam),
and a non-empty exit plan. Entries naming an unknown seam fail. Missing
or unparseable manifests fail closed (exit 1, never assumed permissive).

TC-DEPS-049 (Yoga confinement): production layout targets reach Yoga ONLY
through the single composition-root ``LayoutEngine`` binding. The gate
additionally verifies (a) the manifest binds Yoga to the ``LayoutEngine``
interface and (b) no production source or build file outside the seam
files names Yoga headers, YG* symbols, or Yoga link targets. Test doubles
stay in ``tests/`` (never scanned). Stdlib only.
"""

import argparse
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.deps._deps_data import DepsDataError, load_manifest

DEFAULT_MANIFEST = os.path.join("build", "dependency-manifest.json")

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
)

# TC-DEPS-049: the only production files permitted to name Yoga (headers,
# YG* symbols, link targets). The adapter TU owns Yoga includes; the seam
# binding TU owns the Yoga reference; the CMake lanes own the guarded
# vendored-snapshot wiring. Everything else reaches Yoga through
# runtime/layout_seam.h — or fails this gate.
YOGA_SEAM_FILES = frozenset(
    {
        "engine/runtime/include/runtime/yoga_layout.h",
        "engine/runtime/src/yoga_layout.cpp",
        "engine/runtime/include/runtime/layout_seam.h",
        "engine/runtime/src/layout_seam.cpp",
        "engine/runtime/CMakeLists.txt",
        "engine/runtime/cmake/android-ndk.cmake",
    }
)

# Directories never scanned for Yoga refs (test doubles, vendored
# snapshots, build outputs, and lane records stay out of the gate).
YOGA_SCAN_EXCLUDED_DIRS = frozenset(
    {
        ".git",
        ".gradle",
        "build",
        "dist",
        "node_modules",
        "out",
        "tests",
        "third-party",
        "workflow.local",
    }
)

_YOGA_INCLUDE_RE = re.compile(r"^\s*#\s*include\b.*yoga", re.IGNORECASE)
_YOGA_SYMBOL_RE = re.compile(r"\bYG[A-Z]\w*|yoga::")
_YOGA_LINK_RE = re.compile(r"yoga|yogacore", re.IGNORECASE)

_CPP_EXTS = frozenset({".c", ".cc", ".cpp", ".h", ".hpp"})
_BUILD_FILE_NAMES = frozenset(
    {"CMakeLists.txt", "BUILD", "BUILD.gn", "BUILD.gni"}
)
_BUILD_FILE_EXTS = frozenset({".cmake", ".gn", ".gni"})

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


def _is_yoga_seam_file(relpath):
    """Return True when *relpath* (repo-relative) owns Yoga refs."""
    return relpath.replace(os.sep, "/") in YOGA_SEAM_FILES


def _is_scanned(path):
    """Return True when *path* is a production source/build file to scan."""
    parts = os.path.relpath(path, REPO_ROOT).split(os.sep)
    if any(part in YOGA_SCAN_EXCLUDED_DIRS for part in parts[:-1]):
        return False
    basename = parts[-1]
    ext = os.path.splitext(basename)[1]
    return (
        ext in _CPP_EXTS
        or basename in _BUILD_FILE_NAMES
        or ext in _BUILD_FILE_EXTS
    )


def find_yoga_refs(root):
    """Walk *root*; return ``[(relpath, lineno, line)]`` Yoga refs outside the seam."""
    refs = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(
            d for d in dirnames if d not in YOGA_SCAN_EXCLUDED_DIRS
        )
        for filename in sorted(filenames):
            path = os.path.join(dirpath, filename)
            if not _is_scanned(path):
                continue
            relpath = os.path.relpath(path, root).replace(os.sep, "/")
            if _is_yoga_seam_file(relpath):
                continue
            basename = os.path.basename(filename)
            ext = os.path.splitext(basename)[1]
            is_build = (
                basename in _BUILD_FILE_NAMES or ext in _BUILD_FILE_EXTS
            )
            try:
                with open(path, "r", encoding="utf-8", errors="strict") as fh:
                    for lineno, line in enumerate(fh, 1):
                        text = line.rstrip("\n")
                        if is_build:
                            code = text.split("#", 1)[0]
                            if _YOGA_LINK_RE.search(code):
                                refs.append((relpath, lineno, text.strip()))
                        else:
                            if _YOGA_INCLUDE_RE.search(text) or _YOGA_SYMBOL_RE.search(text):
                                refs.append((relpath, lineno, text.strip()))
            except (OSError, UnicodeError):
                continue
    return refs


def check_yoga_confinement(root, manifest_path):
    """Check Yoga stays behind the LayoutEngine seam; return violations."""
    try:
        entries = load_manifest(manifest_path)
    except DepsDataError as exc:
        return [
            "seam_check: deny: Yoga binding unknown (%s; no seam metadata "
            "covers this input)" % exc
        ]
    yoga = [
        e
        for e in entries
        if isinstance(e, dict)
        and isinstance(e.get("name"), str)
        and e["name"].strip().lower() == "yoga"
    ]
    violations = []
    if not yoga:
        violations.append(
            "seam_check: deny: Yoga has no manifest entry "
            "(not bound to any seam)"
        )
    else:
        for entry in yoga:
            interface = entry.get("interface")
            if not isinstance(interface, str) or interface.strip() != "LayoutEngine":
                violations.append(
                    "seam_check: deny: Yoga bound to %r, not the "
                    "LayoutEngine seam (single composition-root binding "
                    "only)" % (interface,)
                )
    for relpath, lineno, text in find_yoga_refs(root):
        violations.append(
            "seam_check: deny: direct Yoga ref outside the seam: %s:%d: %s "
            "(production targets reach Yoga only via runtime/layout_seam.h)"
            % (relpath, lineno, text)
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

    # Case 6 (TC-DEPS-049): Yoga bound to LayoutEngine with refs confined
    # to the seam files passes.
    import shutil

    def tmp_root(files):
        root = tempfile.mkdtemp()
        for relpath, content in files.items():
            full = os.path.join(root, relpath)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, "w", encoding="utf-8") as fh:
                fh.write(content)
        return root

    def yoga_manifest(interface="LayoutEngine"):
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        json.dump([entry("Yoga", interface, owner="layout")], tmp)
        tmp.close()
        return tmp.name

    root = tmp_root(
        {
            "engine/runtime/src/yoga_layout.cpp": '#include "yoga/Yoga.h"\n',
            "engine/runtime/src/gallery_present.cpp": "// no yoga here\n",
        }
    )
    manifest = yoga_manifest()
    try:
        violations = check_yoga_confinement(root, manifest)
        if violations:
            failures.append("yoga seam pass reported violations: %r" % (violations,))
        else:
            print("self-test: yoga bound to LayoutEngine passes ok")
    finally:
        shutil.rmtree(root, ignore_errors=True)
        os.unlink(manifest)

    # Case 7 (TC-DEPS-049): a direct Yoga include outside the seam fails
    # naming the offending file.
    root = tmp_root(
        {
            "engine/runtime/src/yoga_layout.cpp": '#include "yoga/Yoga.h"\n',
            "engine/runtime/src/renderer.cpp": '#include <yoga/Yoga.h>\n',
        }
    )
    manifest = yoga_manifest()
    try:
        violations = check_yoga_confinement(root, manifest)
        if any("renderer.cpp" in v for v in violations):
            print("self-test: direct yoga include outside seam fails ok")
        else:
            failures.append(
                "direct yoga include not flagged: %r" % (violations,)
            )
    finally:
        shutil.rmtree(root, ignore_errors=True)
        os.unlink(manifest)

    # Case 8 (TC-DEPS-049): Yoga bound to the wrong seam fails.
    root = tmp_root(
        {"engine/runtime/src/yoga_layout.cpp": '#include "yoga/Yoga.h"\n'}
    )
    manifest = yoga_manifest(interface="JsRuntime")
    try:
        violations = check_yoga_confinement(root, manifest)
        if any("LayoutEngine" in v for v in violations):
            print("self-test: yoga on wrong seam fails ok")
        else:
            failures.append("yoga wrong-seam binding not flagged: %r" % (violations,))
    finally:
        shutil.rmtree(root, ignore_errors=True)
        os.unlink(manifest)

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
    violations = list(violations)
    try:
        violations.extend(check_yoga_confinement(REPO_ROOT, args.manifest))
    except OSError as exc:
        print("seam_check: deny: yoga confinement scan failed: %s" % exc)
        return 1
    if violations:
        for violation in violations:
            print(violation)
        return 1
    print("seam_check: ok: all %s manifest entries keep their seams" % len(load_manifest(args.manifest)))
    print("seam_check: ok: Yoga bound to LayoutEngine; no direct Yoga refs outside the seam")
    return 0


if __name__ == "__main__":
    sys.exit(main())
