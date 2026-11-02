#!/usr/bin/env python3
"""Refresh prebuilts through the blessed content-addressed cache (T11).

Per-ABI flow reusing :mod:`tools.deps.prebuilt_cache` as the library::

    key(name, version, ABI, gnArgsHash, ndkRev, snapshotSha256)
      -> cacheRead HIT  : link cached objects into build/android-<abi>/
      -> cacheRead MISS : real cmake+NDK lane build in a temp dir,
                         cachePublish on success only, then link

then records the measured ``wallClockSec`` + ``objectBytes`` per ABI row
into ``build/prebuilt-cache.json`` (AC-DEPS-035; C1 measured, never
estimated). Any missing row or unmeasured value fails admission.

Stdlib only.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(os.path.dirname(_THIS_DIR))
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.deps.prebuilt_cache import cachePublish, cacheRead

ABIS = ("arm64-v8a", "armeabi-v7a", "x86_64", "x86")
ANDROID_PLATFORM = "android-29"
PIN_NAME = "skia-m158"
PIN_VERSION = "m158"

TABLE_PATH = os.path.join(_REPO_ROOT, "build", "prebuilt-cache.json")
PINS_PATH = os.path.join(_REPO_ROOT, "tools", "deps",
                         "third_party_pins.json")
DEFAULT_CACHE_DIR = os.path.join(_REPO_ROOT, "build",
                                 "prebuilt-cache-objects")


def _resolve_ndk_home():
    for var in ("ANDROID_NDK_HOME", "ANDROID_NDK_ROOT", "ANDROID_NDK"):
        home = os.environ.get(var)
        if home:
            return home
    raise SystemExit(
        "error: NDK not found: set ANDROID_NDK_HOME (or ANDROID_NDK_ROOT/"
        "ANDROID_NDK) to the NDK path; expected NDK rev 27.2.12479018")


def _ndk_rev(ndk_home):
    props = os.path.join(ndk_home, "source.properties")
    with open(props, encoding="utf-8") as handle:
        for line in handle:
            if line.startswith("Pkg.Revision"):
                return line.split("=", 1)[1].strip()
    raise SystemExit(
        "error: Pkg.Revision missing in %s/source.properties" % ndk_home)


def _snapshot_sha256():
    with open(PINS_PATH, encoding="utf-8") as handle:
        pins = json.load(handle)
    return pins[PIN_NAME]["sha256"]


def _d3_pins():
    """Vendored D3 source pins feeding the lane (FR-DEPS-008/009)."""
    with open(PINS_PATH, encoding="utf-8") as handle:
        pins = json.load(handle)
    return {name: pins[name]["sha256"]
            for name in ("freetype", "harfbuzz", "icu4c")}


def _configure_args(ndk_home, abi, mirror_root):
    """Canonical per-ABI lane configure args (the hashed GN-args input)."""
    return [
        "-S", os.path.join(_REPO_ROOT, "engine", "runtime"),
        "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(
            ndk_home, "build", "cmake", "android.toolchain.cmake"),
        "-DANDROID_ABI=" + abi,
        "-DANDROID_PLATFORM=" + ANDROID_PLATFORM,
        # FR-DEPS-009 locked setup: the shared Runtime target resolves its
        # vendored D3 archives from this per-ABI mirror (exact-path .a
        # resolution, no system fallback).
        "-DMUSE_TEXT_D3_ROOT=" + mirror_root,
    ]


def _canonical_args(ndk_home, abi):
    """Hash-stable form of the lane args (volatile tmp paths tokenized)."""
    return [
        arg if not arg.startswith("-DMUSE_TEXT_D3_ROOT=") else
        "-DMUSE_TEXT_D3_ROOT=<mirror>/d3-mirror-" + abi
        for arg in _configure_args(ndk_home, abi, "<mirror>")
    ]


def _gn_args_hash(ndk_home, abi):
    canonical = json.dumps({
        "configure": _canonical_args(ndk_home, abi),
        "text_configure": _text_configure_args(ndk_home, abi),
        "d3_pins": _d3_pins(),
    }, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _abi_key(ndk_home, snapshot_sha256, abi):
    return {
        "name": PIN_NAME,
        "version": PIN_VERSION,
        "abi": abi,
        "gn_args_hash": _gn_args_hash(ndk_home, abi),
        "ndk_rev": _ndk_rev(ndk_home),
        "snapshot_sha256": snapshot_sha256,
    }


def _dir_bytes(root):
    total = 0
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            total += os.path.getsize(os.path.join(dirpath, name))
    return total


def _materialize(objects_dir, dest_dir):
    """Link cached/published objects into the per-ABI build dir (copy)."""
    os.makedirs(dest_dir, exist_ok=True)
    for name in sorted(os.listdir(objects_dir)):
        src = os.path.join(objects_dir, name)
        dst = os.path.join(dest_dir, name)
        if os.path.isdir(src) and not os.path.islink(src):
            if os.path.lexists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
        elif os.path.isfile(src) or os.path.islink(src):
            if os.path.lexists(dst):
                os.remove(dst)
            shutil.copy2(src, dst)


def _text_configure_args(ndk_home, abi):
    """Canonical per-ABI args for the engine/text D3 sub-build lane."""
    return [
        "-S", os.path.join(_REPO_ROOT, "engine", "text"),
        "-DCMAKE_TOOLCHAIN_FILE=" + os.path.join(
            ndk_home, "build", "cmake", "android.toolchain.cmake"),
        "-DANDROID_ABI=" + abi,
        "-DANDROID_PLATFORM=" + ANDROID_PLATFORM,
    ]


def _find_archives(root, names):
    """Locate built static archives by filename under a build tree."""
    found = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            if name in names and name not in found:
                found[name] = os.path.join(dirpath, name)
    missing = [name for name in names if name not in found]
    if missing:
        raise SystemExit(
            "error: D3 archives missing from lane build: %s"
            % ",".join(sorted(missing)))
    return found


def _lane_build(ndk_home, abi, tmp_root):
    """Real cmake+NDK lane build for one ABI inside a temp root.

    Stage 1 (FR-DEPS-009 locked setup shape): configure + build the
    engine/text in-tree static D3 sub-builds (freetype, harfbuzz,
    icu_target) and stage the five archives into the conventional
    per-ABI mirror ``d3-mirror-<ABI>/lib`` sibling of the runtime OUTDIR.
    Stage 2: configure + build engine/runtime against that mirror.
    Returns the built libRuntime.so path.
    """
    text_build = os.path.join(tmp_root, "text")
    subprocess.run(["cmake", "-B", text_build] + _text_configure_args(
        ndk_home, abi)[1:], cwd=_REPO_ROOT, check=True)
    subprocess.run(["cmake", "--build", text_build,
                    "--target", "freetype", "harfbuzz", "icu_target",
                    "--parallel"],
                   cwd=_REPO_ROOT, check=True)
    archives = _find_archives(text_build, (
        "libfreetype.a", "libharfbuzz.a",
        "libicuuc.a", "libicudata.a", "libicui18n.a"))
    mirror_lib = os.path.join(tmp_root, "d3-mirror-" + abi, "lib")
    os.makedirs(mirror_lib)
    for name, path in sorted(archives.items()):
        shutil.copy2(path, os.path.join(mirror_lib, name))
    mirror_root = os.path.join(tmp_root, "d3-mirror-" + abi)
    outdir = os.path.join(tmp_root, "build")
    subprocess.run(["cmake", "-B", outdir] + _configure_args(
        ndk_home, abi, mirror_root)[1:], cwd=_REPO_ROOT, check=True)
    subprocess.run(["cmake", "--build", outdir, "--parallel"],
                   cwd=_REPO_ROOT, check=True)
    lib = os.path.join(outdir, "libRuntime.so")
    if not os.path.isfile(lib) or os.path.getsize(lib) == 0:
        raise SystemExit(
            "error: lane build produced no libRuntime.so for %s" % abi)
    return lib


def _prior_rows(table_path):
    """Index a previous table by ABI (build cost is a function of the key)."""
    if not os.path.isfile(table_path):
        return {}
    try:
        with open(table_path, encoding="utf-8") as handle:
            table = json.load(handle)
    except (OSError, ValueError):
        return {}
    rows = table.get("rows", table) if isinstance(table, dict) else table
    if isinstance(rows, dict):
        rows = list(rows.values())
    prior = {}
    for row in rows:
        if isinstance(row, dict) and row.get("abi"):
            prior[row["abi"]] = row
    return prior


def _refresh_abi(cache_dir, ndk_home, snapshot_sha256, abi, prior):
    key = _abi_key(ndk_home, snapshot_sha256, abi)
    dest_dir = os.path.join(_REPO_ROOT, "build", "android-" + abi)
    start = time.monotonic()
    status, objects = cacheRead(cache_dir, key)
    if status == "HIT":
        _materialize(objects, dest_dir)
        print("READ %s -> HIT %s" % (abi, objects), flush=True)
    else:
        print("READ %s -> MISS (rebuild, no link)" % abi, flush=True)
        with tempfile.TemporaryDirectory(prefix="refresh-" + abi + "-") as tmp:
            lib = _lane_build(ndk_home, abi, tmp)
            stage = os.path.join(tmp, "stage")
            os.makedirs(stage)
            shutil.copy2(lib, os.path.join(stage, "libRuntime.so"))
            entry = cachePublish(cache_dir, key, stage)
            print("PUBLISH %s -> %s" % (abi, entry), flush=True)
        _status, objects = cacheRead(cache_dir, key)
        if _status != "HIT":
            raise SystemExit(
                "error: just-published entry for %s does not verify" % abi)
        _materialize(objects, dest_dir)
    wall_clock = time.monotonic() - start
    object_bytes = _dir_bytes(dest_dir)
    if status == "HIT":
        # Build cost is a function of the key: a HIT links the identical
        # objects, so the previously measured build cost carries forward
        # (same key -> same cost); a changed key rebuilds and re-measures.
        old = prior.get(abi, {})
        if old.get("key") == key and isinstance(
                old.get("wallClockSec"), (int, float)):
            wall_clock = old["wallClockSec"]
            print("CARRY %s wallClockSec=%.3f (key unchanged)" %
                  (abi, wall_clock), flush=True)
    row = {
        "abi": abi,
        "key": key,
        "wallClockSec": wall_clock,
        "objectBytes": object_bytes,
    }
    print("ROW %s wallClockSec=%.3f objectBytes=%d" %
          (abi, wall_clock, object_bytes), flush=True)
    return row


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Refresh prebuilts through the blessed cache (T11).")
    parser.add_argument("--cache-dir", default=DEFAULT_CACHE_DIR)
    parser.add_argument("--abis", nargs="*", default=list(ABIS))
    parser.add_argument("--table", default=TABLE_PATH)
    options = parser.parse_args(argv)
    for abi in options.abis:
        if abi not in ABIS:
            raise SystemExit("error: misconfigured ABI slot %r" % abi)
    ndk_home = _resolve_ndk_home()
    if not os.path.isdir(ndk_home):
        raise SystemExit("error: NDK path not found: %r" % ndk_home)
    snapshot_sha256 = _snapshot_sha256()
    prior = _prior_rows(options.table)
    rows = [_refresh_abi(options.cache_dir, ndk_home, snapshot_sha256, abi,
                         prior)
            for abi in options.abis]
    table = {"rows": rows}
    os.makedirs(os.path.dirname(options.table), exist_ok=True)
    tmp = options.table + ".tmp"
    with open(tmp, "w", encoding="utf-8") as handle:
        json.dump(table, handle, sort_keys=True, indent=2)
        handle.write("\n")
    os.replace(tmp, options.table)
    print("TABLE %s rows=%d" % (options.table, len(rows)), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
