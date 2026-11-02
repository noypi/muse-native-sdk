#!/usr/bin/env python3
"""Batch 4 (T9) hermetic unit tests for the blessed content-addressed prebuilt cache.

Covers FR-DEPS-012 / AC-DEPS-031 + TC-DEPS-031 walkthrough steps: key shape,
immutability, atomic publish, verify-on-read, mismatch-rebuild, failed-invisible.

Hermetic: stdlib `unittest` + `hashlib` + `tempfile` only. Every fixture lives
in a `TemporaryDirectory` owned by the test process; no network, no `.env`,
no application configuration, no live service, no subprocess.

Public boundary under test (plan.md T9 `Interface:` verbatim):
  `cacheRead(key{name,version,ABI,gnArgsHash,ndkRev,snapshotSha256}) -> HIT(objects) | MISS(rebuild)`;
  `cachePublish(tmpDir,key) -> atomic move on success only`.
"""

import hashlib
import json
import os
import sys
import tempfile
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from tools.deps.prebuilt_cache import CacheError, cachePublish, cacheRead

KEY_FIELDS = ("name", "version", "abi", "gn_args_hash", "ndk_rev",
              "snapshot_sha256")


def _good_key(**overrides):
    key = {
        "name": "skia-m158",
        "version": "m158",
        "abi": "arm64-v8a",
        "gn_args_hash": hashlib.sha256(b"gn-args").hexdigest(),
        "ndk_rev": "r27",
        "snapshot_sha256": hashlib.sha256(b"snapshot").hexdigest(),
    }
    key.update(overrides)
    return key


def _seed_entry(cache_dir, key, payload=b"objects"):
    """Simulate a successful build+publish so reads have something to hit."""
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "out"))
        with open(os.path.join(tmp, "out", "libskia.a"), "wb") as f:
            f.write(payload)
        return cachePublish(cache_dir, key, tmp)


class TestCacheKeyShape(unittest.TestCase):
    def test_key_holds_all_six_fields(self):
        key = _good_key()
        self.assertEqual(set(key), set(KEY_FIELDS))

    def test_read_rejects_key_missing_field(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            del key["snapshot_sha256"]
            with self.assertRaises((ValueError, KeyError)):
                cacheRead(cache, key)


class TestCacheReadPublish(unittest.TestCase):
    def test_miss_on_empty_cache_triggers_rebuild_not_link(self):
        with tempfile.TemporaryDirectory() as cache:
            status, _ = cacheRead(cache, _good_key())
            self.assertEqual(status, "MISS")

    def test_hit_after_publish_returns_objects(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            _seed_entry(cache, key)
            status, objects = cacheRead(cache, key)
            self.assertEqual(status, "HIT")
            self.assertTrue(os.path.isdir(objects))

    def test_verify_on_read_rejects_tampered_objects(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            _seed_entry(cache, key, payload=b"original")
            # Tamper with the published objects in place.
            status, objects = cacheRead(cache, key)
            self.assertEqual(status, "HIT")
            with open(os.path.join(objects, "libskia.a"), "wb") as f:
                f.write(b"tampered")
            status, _ = cacheRead(cache, key)
            self.assertEqual(status, "MISS")

    def test_mismatch_key_field_rebuilds_instead_of_linking(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            _seed_entry(cache, key)
            altered = _good_key(gn_args_hash=hashlib.sha256(b"other").hexdigest())
            status, _ = cacheRead(cache, altered)
            self.assertEqual(status, "MISS")

    def test_republish_same_key_raises_and_preserves_entry(self):
        import glob as _glob
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            _seed_entry(cache, key, payload=b"original")
            status, objects = cacheRead(cache, key)
            self.assertEqual(status, "HIT")
            with open(os.path.join(objects, "out", "libskia.a"), "rb") as f:
                before = f.read()
            with tempfile.TemporaryDirectory() as tmp:
                os.makedirs(os.path.join(tmp, "out"))
                with open(os.path.join(tmp, "out", "libskia.a"), "wb") as f:
                    f.write(b"replacement")
                with self.assertRaises(CacheError):
                    cachePublish(cache, key, tmp)
            # Entry bytes unchanged and still a HIT.
            status, objects = cacheRead(cache, key)
            self.assertEqual(status, "HIT")
            with open(os.path.join(objects, "out", "libskia.a"), "rb") as f:
                self.assertEqual(f.read(), b"original")
            self.assertEqual(len(_glob.glob(os.path.join(cache, "*"))), 1)

    def test_tampered_manifest_misses_and_preserves_entry(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            entry = _seed_entry(cache, key, payload=b"original")
            manifest_path = os.path.join(entry, "cache_entry.json")
            with open(manifest_path, encoding="utf-8") as f:
                manifest = json.load(f)
            manifest["files"] = {"out/libskia.a": "0" * 64}
            with open(manifest_path, "w", encoding="utf-8") as f:
                json.dump(manifest, f, sort_keys=True, indent=2)
            status, _ = cacheRead(cache, key)
            self.assertEqual(status, "MISS")
            self.assertTrue(os.path.isdir(entry))

    def test_failed_build_never_visible_in_cache(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            with tempfile.TemporaryDirectory() as tmp:
                with open(os.path.join(tmp, "BUILD_FAILED"), "w") as f:
                    f.write("link failed")
                with self.assertRaises(Exception):
                    cachePublish(cache, key, tmp)
            status, _ = cacheRead(cache, key)
            self.assertEqual(status, "MISS")

    def test_publish_leaves_no_partial_entry(self):
        with tempfile.TemporaryDirectory() as cache:
            key = _good_key()
            with tempfile.TemporaryDirectory() as tmp:
                with open(os.path.join(tmp, "BUILD_FAILED"), "w") as f:
                    f.write("boom")
                try:
                    cachePublish(cache, key, tmp)
                except Exception:
                    pass
            self.assertEqual(os.listdir(cache), [])


if __name__ == "__main__":
    unittest.main()
