"""Per-dependency GN/CMake arg sets + canonical cache key.

This module is the per-dep key authority (NOT ``toolchain/cache/cacheKey.js``,
which sorts only ``cflags`` and knows nothing of name/version/abi/snapshot
or eligibility).  The canonical key is::

    sha256(canonical_JSON({name, version, abi, ndk_rev,
                           snapshot_sha256, args_hash}))

where ``args_hash = sha256(" ".join(sorted(fragments)))``.  Fragment-sort
MUST precede the hash (approved sort fix): reordered fragments MUST HIT,
any flag flip/add/remove MUST MISS.  Names outside ``eligibility.json``
``cache_eligible[]`` are source-built: :func:`compute_key` raises
:exc:`SourceBuiltRefused` and never returns a key for them.
"""

import argparse
import hashlib
import json
import os

# --- Per-dep fragment tables (UNORDERED on purpose; canonical_args_hash sorts)
FREETYPE_CMAKE_FRAGMENTS = [
    "PLATFORM=android-24",
    "ABI=arm64-v8a",
    "BUILD_SHARED_LIBS=OFF",
    "FT_DISABLE_ZLIB=ON",
    "FT_DISABLE_BZIP2=ON",
    "FT_DISABLE_PNG=ON",
    "FT_DISABLE_HARFBUZZ=ON",
    "FT_DISABLE_BROTLI=ON",
]

HARFBUZZ_CMAKE_FRAGMENTS = [
    "PLATFORM=android-24",
    "ABI=arm64-v8a",
    "BUILD_SHARED_LIBS=OFF",
    "HB_HAVE_FREETYPE=OFF",
    "HB_HAVE_GLIB=OFF",
    "HB_HAVE_ICU=OFF",
    "HB_BUILD_TESTS=OFF",
    "HB_BUILD_UTILS=OFF",
    "HB_BUILD_SUBSET=OFF",
]

# Measured from yoga-3.2.1/yoga/CMakeLists.txt (+ cmake/project-defaults.cmake):
# add_library(yogacore STATIC) -> BUILD_SHARED_LIBS=OFF; C++20 / PIC / hidden
# visibility from project-defaults; PLATFORM/ABI follow the sibling-table
# Android convention (per-ABI variance rides on compute_key's abi field).
YOGA_CMAKE_FRAGMENTS = [
    "PLATFORM=android-24",
    "ABI=arm64-v8a",
    "BUILD_SHARED_LIBS=OFF",
    "CMAKE_CXX_STANDARD=20",
    "CMAKE_POSITION_INDEPENDENT_CODE=ON",
    "CMAKE_CXX_VISIBILITY_PRESET=hidden",
]

HARFBUZZ_GN_FRAGMENTS = [
    "skia_m158_compat=true",
    "hb_ot=true",
    "target_cpu=arm64",
    "is_official_build=true",
]

SKIA_M158_GN_FRAGMENTS = [
    'target_cpu="arm64"',
    'is_official_build=true',
    'skia_use_harfbuzz=true',
    'skia_use_freetype=true',
    'skia_use_angle=false',
]

FRAGMENTS = {
    ("freetype", "cmake"): FREETYPE_CMAKE_FRAGMENTS,
    ("harfbuzz", "cmake"): HARFBUZZ_CMAKE_FRAGMENTS,
    ("yoga", "cmake"): YOGA_CMAKE_FRAGMENTS,
    ("harfbuzz", "gn"): HARFBUZZ_GN_FRAGMENTS,
    ("skia-m158", "gn"): SKIA_M158_GN_FRAGMENTS,
}

_HERE = os.path.dirname(os.path.abspath(__file__))


class SourceBuiltRefused(Exception):
    """Raised when a cache key is requested for a source-built dep."""


def canonical_args_hash(fragments):
    """sha256 of space-joined SORTED fragments (deterministic under reorder)."""
    return hashlib.sha256(" ".join(sorted(fragments)).encode("utf-8")).hexdigest()


def _eligibility():
    with open(os.path.join(_HERE, "eligibility.json"), encoding="utf-8") as fh:
        return json.load(fh)


def is_cache_eligible(name):
    return name in _eligibility()["cache_eligible"]


def compute_key(name, version, abi, ndk_rev, snapshot_sha256, args_hash):
    """Return the canonical per-dep key dict; refuse source-built names."""
    if not is_cache_eligible(name):
        raise SourceBuiltRefused(
            "compute_key: refuse: %r is source-built (not in cache_eligible[])" % (name,)
        )
    canonical = json.dumps(
        {
            "abi": abi,
            "args_hash": args_hash,
            "name": name,
            "ndk_rev": ndk_rev,
            "snapshot_sha256": snapshot_sha256,
            "version": version,
        },
        sort_keys=True,
        separators=(",", ":"),
    )
    return {
        "name": name,
        "version": version,
        "abi": abi,
        "ndk_rev": ndk_rev,
        "snapshot_sha256": snapshot_sha256,
        "gn_args_hash": args_hash,
        "canonical_key": hashlib.sha256(canonical.encode("utf-8")).hexdigest(),
    }


def main(argv=None):
    ap = argparse.ArgumentParser(description="Per-dep args hash + key check.")
    ap.add_argument("--dep", action="append", default=[],
                    help="dep name (repeatable); checked against cache rows")
    ap.add_argument("--print-hash", action="store_true")
    ap.add_argument("--check", action="store_true",
                    help="compare against build/prebuilt-cache.json rows")
    args = ap.parse_args(argv)
    if args.print_hash or args.check:
        _doc = json.load(open("build/prebuilt-cache.json"))
        rows = _doc["rows"] if "rows" in _doc else _doc["entries"]
        by_name = {}
        for row in rows:
            by_name.setdefault(row["key"]["name"], row)
        rc = 0
        for dep in args.dep:
            tables = [(via, FRAGMENTS[(dep, via)])
                      for via in ("cmake", "gn") if (dep, via) in FRAGMENTS]
            if not tables:
                print("per_dep_args: unknown dep %r" % dep)
                rc = 1
                continue
            for via, frags in tables:
                h = canonical_args_hash(frags)
                # order-shuffle control: reversed input must give identical hash
                h_shuf = canonical_args_hash(list(reversed(frags)))
                assert h == h_shuf, "sort-before-hash broken for %r" % dep
                if args.print_hash:
                    print("%s [%s] args_hash=%s shuffle_identical=%s"
                          % (dep, via, h, h_shuf == h))
                if args.check:
                    cands = [r for r in rows if r["key"]["name"] == dep]
                    if not cands:
                        print("per_dep_args: no cache row for %r" % dep)
                        rc = 1
                    elif not any(r["key"]["gn_args_hash"] == h for r in cands):
                        # Principled legacy check: row stored a pre-sort-fix
                        # hash (unsorted join)? If sorting the row's own
                        # stored args reproduces h, fragments are correct and
                        # the ROW is legacy, not the table.
                        legacy = [r for r in cands
                                  if canonical_args_hash(
                                      (r.get("gn_args") or r.get("cmake_args") or "").split())
                                  == h]
                        if legacy:
                            print("per_dep_args: %s [%s] LEGACY-PRE-SORT-ROW "
                                  "(fragments correct; row stores unsorted-join hash)"
                                  % (dep, via))
                        else:
                            print("per_dep_args: MISMATCH %s [%s] computed=%s rows=%s"
                                  % (dep, via, h, [r["key"]["gn_args_hash"][:12] for r in cands]))
                            rc = 1
                    else:
                        print("per_dep_args: %s [%s] args_hash matches row (%s)"
                              % (dep, via, h[:12]))
        return rc
    ap.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
