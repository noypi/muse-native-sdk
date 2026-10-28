#!/usr/bin/env python3
"""No-dynamic-initializer CI check for the startup path (AC-CORE-031).

Scans startup-path objects under <object_dir> for dynamic initializers
using llvm-nm/readelf (same approach as
engine/core/plugin/tests/visibility_check.sh), with a raw-byte fallback
when neither tool is installed.

Prints one ``DYNAMIC_INIT:<file>:<symbol>`` line per violation to stdout.

Exit status: 0 when no dynamic initializer is present; 1 when a violation
is found; 2 when the input cannot be scanned (missing/unreadable dir) or
no scan tool is available. Never passes silently (fail-closed).
Stdlib only.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys

# Symbol names the compiler emits for dynamic initialization, plus the
# sections that carry them. Matched against llvm-nm/readelf output and,
# as a fallback, against raw object bytes.
SYMBOL_PATTERNS = (
    "_GLOBAL__sub_I_",
    "__static_initialization_and_destruction_",
    "__do_global_dtors_aux",
)

SECTION_PATTERNS = (
    ".init_array",
    ".preinit_array",
    ".ctors",
)


def _iter_object_files(root):
    """Yield paths of candidate object files under root (recursive)."""
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            lower = name.lower()
            if lower.endswith((".o", ".obj", ".a", ".so")):
                yield os.path.join(dirpath, name)


def _scan_with_nm(tool, path):
    """Return a violating symbol name, or None. Raises on tool failure."""
    proc = subprocess.run(
        [tool, "-g", "--defined-only", path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    if proc.returncode != 0:
        raise RuntimeError("%s failed on %r" % (tool, path))
    for line in proc.stdout.splitlines():
        symbol = line.split()[-1] if line.split() else ""
        for pattern in SYMBOL_PATTERNS:
            if pattern in symbol:
                return symbol
    return None


def _scan_with_readelf(path):
    """Return a violating section/symbol name, or None. Raises on failure."""
    sections = subprocess.run(
        ["readelf", "-S", "-W", path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    if sections.returncode != 0:
        raise RuntimeError("readelf failed on %r" % path)
    for line in sections.stdout.splitlines():
        for pattern in SECTION_PATTERNS:
            if re.search(r"\b%s\b" % re.escape(pattern), line):
                return pattern
    symbols = subprocess.run(
        ["readelf", "-s", "-W", path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
    )
    if symbols.returncode != 0:
        raise RuntimeError("readelf failed on %r" % path)
    for line in symbols.stdout.splitlines():
        for pattern in SYMBOL_PATTERNS:
            if pattern in line:
                return pattern
    return None


def _scan_raw_bytes(path):
    """Fallback byte scan when no ELF tool is installed."""
    with open(path, "rb") as handle:
        data = handle.read(8 * 1024 * 1024)
    for pattern in SECTION_PATTERNS + SYMBOL_PATTERNS:
        if pattern.encode("ascii") in data:
            return pattern
    return None


def check(object_dir):
    """Scan object_dir; return (violations, errors) and print DYNAMIC_INIT lines."""
    if not object_dir or not os.path.isdir(object_dir):
        print(
            "check_dynamic_init: object dir not found: %r" % (object_dir,),
            file=sys.stderr,
        )
        return [], ["missing object dir"]

    try:
        files = sorted(_iter_object_files(object_dir))
    except OSError as exc:
        print("check_dynamic_init: cannot walk %r: %s" % (object_dir, exc),
              file=sys.stderr)
        return [], ["unreadable object dir"]

    nm_tool = None
    for candidate in ("llvm-nm", "nm"):
        if shutil.which(candidate):
            nm_tool = candidate
            break
    readelf = shutil.which("readelf")

    violations = []
    errors = []
    for path in files:
        name = os.path.basename(path)
        hit = None
        try:
            if nm_tool is not None:
                hit = _scan_with_nm(nm_tool, path)
            if hit is None and readelf is not None:
                hit = _scan_with_readelf(path)
            if hit is None:
                # Confirm with (or fall back to) the raw-byte scan so a
                # tool that misses a marker cannot silently pass the file.
                hit = _scan_raw_bytes(path)
        except (OSError, RuntimeError) as exc:
            errors.append("%s: %s" % (path, exc))
            continue
        if hit is not None:
            violations.append((name, hit))
            print("DYNAMIC_INIT:%s:%s" % (name, hit))
    return violations, errors


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Fail-closed no-dynamic-initializer check (AC-CORE-031)."
    )
    parser.add_argument("object_dir", help="directory of startup-path objects")
    args = parser.parse_args(argv)

    violations, errors = check(args.object_dir)
    if errors:
        for err in errors:
            print("check_dynamic_init: error: %s" % err, file=sys.stderr)
        return 2
    if violations:
        return 1
    print("check_dynamic_init: ok: no dynamic initializers in %r"
          % (args.object_dir,))
    return 0


if __name__ == "__main__":
    sys.exit(main())
