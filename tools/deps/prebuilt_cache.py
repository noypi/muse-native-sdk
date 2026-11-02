#!/usr/bin/env python3
"""Blessed content-addressed prebuilt cache (FR-DEPS-012, AC-DEPS-031).

One immutable entry per key
``{name, version, abi, gn_args_hash, ndk_rev, snapshot_sha256}``.
Builds happen in a caller-owned temp dir; :func:`cachePublish` moves the
result into the cache atomically on success only. :func:`cacheRead`
verifies pin + object hashes on every read; any mismatch rebuilds
(``MISS``) instead of linking. Failed builds are never visible.

Stdlib only.
"""

import hashlib
import json
import os
import shutil
import tempfile

__all__ = ["cacheRead", "cachePublish", "CacheError", "KEY_FIELDS"]

KEY_FIELDS = ("name", "version", "abi", "gn_args_hash", "ndk_rev",
              "snapshot_sha256")

_BUILD_FAILED_MARKER = "BUILD_FAILED"
_ENTRY_MANIFEST = "cache_entry.json"
_OBJECTS_DIR = "objects"


class CacheError(Exception):
    """Raised when a build result cannot be published (violation -> fail)."""


def _check_key(key):
    if not isinstance(key, dict):
        raise ValueError("cache key must be a dict")
    missing = [f for f in KEY_FIELDS if f not in key]
    if missing:
        raise ValueError("cache key missing fields: %s" % ",".join(missing))
    for field in KEY_FIELDS:
        value = key[field]
        if not isinstance(value, str) or not value:
            raise ValueError("cache key field %r must be a non-empty str" % field)


def _entry_name(key):
    canonical = json.dumps({f: key[f] for f in KEY_FIELDS},
                           sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _entry_dir(cache_dir, key):
    return os.path.join(cache_dir, _entry_name(key))


def _objects_dir(entry_dir):
    return os.path.join(entry_dir, _OBJECTS_DIR)


def _hash_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _scan_files(root):
    records = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            records[rel] = _hash_file(full)
    return records


def cachePublish(cache_dir, key, tmp_dir):
    """Publish a successful build into the cache.

    Atomically moves ``tmp_dir`` contents into the entry for ``key``.
    A ``BUILD_FAILED`` marker in ``tmp_dir`` (or any other failure)
    raises and leaves the cache untouched — failed builds never visible.
    """
    _check_key(key)
    if not isinstance(cache_dir, str) or not cache_dir:
        raise ValueError("cache_dir must be a non-empty str")
    if not isinstance(tmp_dir, str) or not os.path.isdir(tmp_dir):
        raise ValueError("tmp_dir must be an existing directory")
    marker = os.path.join(tmp_dir, _BUILD_FAILED_MARKER)
    if os.path.exists(marker):
        raise CacheError("build failed; refusing to publish into cache")
    os.makedirs(cache_dir, exist_ok=True)
    entry = _entry_dir(cache_dir, key)
    staging_parent = tempfile.mkdtemp(dir=cache_dir)
    try:
        staging = os.path.join(staging_parent, "entry")
        objects = os.path.join(staging, _OBJECTS_DIR)
        os.makedirs(objects)
        for name in sorted(os.listdir(tmp_dir)):
            src = os.path.join(tmp_dir, name)
            dst = os.path.join(objects, name)
            if os.path.isdir(src) and not os.path.islink(src):
                shutil.copytree(src, dst)
            elif os.path.isfile(src) or os.path.islink(src):
                shutil.copy2(src, dst)
        records = _scan_files(objects)
        if not records:
            raise CacheError("nothing to publish: build produced no objects")
        manifest = {
            "key": {f: key[f] for f in KEY_FIELDS},
            "files": records,
        }
        with open(os.path.join(staging, _ENTRY_MANIFEST), "w",
                  encoding="utf-8") as handle:
            json.dump(manifest, handle, sort_keys=True, indent=2)
        # Immutability (AC-DEPS-031): never mutate a published entry.
        # Republish of an existing key fails closed; replacement/deletion
        # is a separate privileged op, out of scope here.
        if os.path.lexists(entry):
            raise CacheError("entry already published for key; refusing to clobber")
        os.replace(staging, entry)
    except BaseException:
        shutil.rmtree(staging_parent, ignore_errors=True)
        raise
    else:
        shutil.rmtree(staging_parent, ignore_errors=True)
    return entry


def cacheRead(cache_dir, key):
    """Read the cache entry for ``key``.

    Returns ``("HIT", objects_dir)`` when the entry exists and its
    pin + object hashes verify, else ``("MISS", None)`` (rebuild path).
    """
    _check_key(key)
    entry = _entry_dir(cache_dir, key) if cache_dir else None
    if not entry or not os.path.isdir(entry):
        return ("MISS", None)
    manifest_path = os.path.join(entry, _ENTRY_MANIFEST)
    objects = _objects_dir(entry)
    try:
        with open(manifest_path, encoding="utf-8") as handle:
            manifest = json.load(handle)
        if manifest.get("key") != {f: key[f] for f in KEY_FIELDS}:
            raise CacheError("key mismatch")
        expected = manifest.get("files")
        if not isinstance(expected, dict) or not expected:
            raise CacheError("empty file record")
        if not os.path.isdir(objects):
            raise CacheError("objects missing")
        actual = _scan_files(objects)
        if actual != expected:
            raise CacheError("object hash mismatch")
    except (OSError, ValueError, CacheError):
        # Mismatch rebuilds instead of linking: report MISS without
        # deleting. The poisoned entry is left for audit; deletion is a
        # separate privileged op, out of scope for the read path.
        return ("MISS", None)
    return ("HIT", objects)
