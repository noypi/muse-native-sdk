"""Batch 4 — FFmpeg LGPL-configuration check (T7).

Covers FR-DEPS-007 (AC-DEPS-017, AC-DEPS-018) per
specs-cpp-dependencies.md.

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — ``tools/deps/ffmpeg_check.py``
is absent so every CLI invocation fails (greenfield for batch 4).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "ffmpeg_check.py")


def run_cli(*args):
    """Run the check CLI hermetically; return CompletedProcess."""
    return subprocess.run(
        [sys.executable, SCRIPT, *args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        timeout=60,
    )


def write_manifest(entries):
    """Write a tmp manifest from a list of entry dicts; return its path."""
    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".json", delete=False, encoding="utf-8"
    )
    json.dump(entries, tmp)
    tmp.close()
    return tmp.name


def full_entry(name, **overrides):
    """A manifest entry carrying every field the manifest schema requires."""
    data = {
        "name": name,
        "owner": "media",
        "interface": "CodecModule",
        "exit_plan": "replace decoders with platform MediaCodec",
        "licence": "LGPL-2.1-or-later",
        "link": "dynamic",
    }
    data.update(overrides)
    return data


LGPL_CONFIGURE = "--enable-shared --disable-static --disable-gpl --disable-nonfree"


def ffmpeg_entry(**overrides):
    """An FFmpeg manifest entry in the admitted LGPL shared-library shape."""
    data = full_entry("FFmpeg", configure=LGPL_CONFIGURE)
    data.update(overrides)
    return data


class TestFfmpegCheck(unittest.TestCase):
    """T7 CLI: exit 0 (shared + LGPL configure) | exit 1 (static/GPL flags/GPL codec) | exit 2 (usage)."""

    def test_lgpl_shared_passes(self):
        """FFmpeg as a shared library with an LGPL configure line -> exit 0 (AC-DEPS-017 happy path)."""
        path = write_manifest([ffmpeg_entry()])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_no_ffmpeg_passes_vacuously(self):
        """A manifest with no FFmpeg entry passes (nothing to refuse)."""
        path = write_manifest([full_entry("HarfBuzz", licence="MIT", link="static")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_static_link_fails(self):
        """FFmpeg statically linked into the application fails (AC-DEPS-018)."""
        path = write_manifest([ffmpeg_entry(link="static")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_enable_gpl_fails_naming_flag(self):
        """A configure line with --enable-gpl fails naming the flag (AC-DEPS-018)."""
        path = write_manifest(
            [ffmpeg_entry(configure=LGPL_CONFIGURE + " --enable-gpl")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("--enable-gpl", proc.stdout + proc.stderr)

    def test_enable_nonfree_fails_naming_flag(self):
        """A configure line with --enable-nonfree fails naming the flag (AC-DEPS-018)."""
        path = write_manifest(
            [ffmpeg_entry(configure=LGPL_CONFIGURE + " --enable-nonfree")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("--enable-nonfree", proc.stdout + proc.stderr)

    def test_shared_spelling_passes(self):
        """FFmpeg with link "shared" passes (parity with lgpl_check, AC-DEPS-017)."""
        path = write_manifest([ffmpeg_entry(link="shared")])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_gpl_only_codec_libx265_fails(self):
        """A configure line with --enable-libx265 fails (AC-DEPS-018)."""
        path = write_manifest(
            [ffmpeg_entry(configure=LGPL_CONFIGURE + " --enable-libx265")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("libx265", (proc.stdout + proc.stderr).lower())

    def test_gpl_only_codec_libxvid_fails(self):
        """A configure line with --enable-libxvid fails (AC-DEPS-018)."""
        path = write_manifest(
            [ffmpeg_entry(configure=LGPL_CONFIGURE + " --enable-libxvid")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("libxvid", (proc.stdout + proc.stderr).lower())

    def test_gpl_only_codec_fails(self):
        """A configure line enabling a GPL-only codec (libx264) fails (AC-DEPS-018)."""
        path = write_manifest(
            [ffmpeg_entry(configure=LGPL_CONFIGURE + " --enable-libx264")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        combined = (proc.stdout + proc.stderr).lower()
        self.assertTrue(
            "libx264" in combined or "gpl" in combined,
            "failure must name the GPL-only codec or the GPL condition: %r"
            % (proc.stdout + proc.stderr),
        )

    def test_missing_configure_fails(self):
        """An FFmpeg entry with no recorded configure line fails (AC-DEPS-017 requires it recorded)."""
        entry = ffmpeg_entry()
        del entry["configure"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_missing_manifest_fails_closed(self):
        """Missing manifest is exit 1 (fail closed), not success or usage error."""
        proc = run_cli(
            "--manifest", os.path.join(REPO_ROOT, "does-not-exist-xyz.json")
        )
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_unparseable_manifest_fails_closed(self):
        """Unparseable manifest is exit 1 (fail closed)."""
        tmp = tempfile.NamedTemporaryFile(
            mode="w", suffix=".json", delete=False, encoding="utf-8"
        )
        tmp.write("{ this is not json !!!")
        tmp.close()
        try:
            proc = run_cli("--manifest", tmp.name)
        finally:
            os.unlink(tmp.name)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)

    def test_self_test_passes(self):
        """Bundled self-test (LGPL-shared pass) exits 0 (TC-DEPS-017)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_self_test_gpl_passes(self):
        """Bundled GPL-fixture self-test (correctly refused) exits 0 (TC-DEPS-018)."""
        proc = run_cli("--self-test-gpl")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
