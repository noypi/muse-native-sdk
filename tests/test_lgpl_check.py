"""Batch 4 — LGPL conditional-admission check (T6, part 2 of 2).

Covers FR-DEPS-006 (AC-DEPS-014) per specs-cpp-dependencies.md.

Hermetic: stdlib unittest only, tmp manifests, no network/secrets/services.
Expected state before implementation: RED — ``tools/deps/lgpl_check.py``
is absent so every CLI invocation fails (greenfield for batch 4).
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRIPT = os.path.join(REPO_ROOT, "tools", "deps", "lgpl_check.py")


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
        "owner": "core",
        "interface": "TestInterface",
        "exit_plan": "replace with local implementation",
        "licence": "MIT",
        "link": "static",
    }
    data.update(overrides)
    return data


def lgpl_dynamic_entry(name="ffmpeg-test-lib-xyz"):
    """An LGPL entry satisfying every AC-DEPS-014 condition."""
    return full_entry(
        name,
        licence="LGPL-2.1-or-later",
        link="dynamic",
        licence_text="third-party/%s-LICENSE (LGPL text)" % name,
        source_offer="exact sources including local patches offered per LGPL section 6",
        relinkable_objects="%s-relinkable-objects.zip recorded in SBOM" % name,
    )


class TestLgplCheck(unittest.TestCase):
    """T6 CLI: exit 0 (all LGPL dynamic + artifacts) | exit 1 (static/missing) | exit 2 (usage)."""

    def test_non_lgpl_manifest_passes(self):
        """A manifest with no LGPL entries passes vacuously (AC-DEPS-014 happy path)."""
        path = write_manifest(
            [full_entry("lib-a", licence="MIT"), full_entry("lib-b", licence="Apache-2.0")]
        )
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_lgpl_dynamic_with_artifacts_passes(self):
        """LGPL + dynamic link + licence text + source offer + relinkable objects -> exit 0 (AC-DEPS-014 happy path)."""
        path = write_manifest([lgpl_dynamic_entry()])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_lgpl_static_fails(self):
        """LGPL with a static link fails admission (AC-DEPS-014)."""
        entry = lgpl_dynamic_entry("static-lgpl-lib-xyz")
        entry["link"] = "static"
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("static-lgpl-lib-xyz", proc.stdout + proc.stderr)

    def test_lgpl_missing_licence_text_fails(self):
        """LGPL without recorded licence text fails (AC-DEPS-014)."""
        entry = lgpl_dynamic_entry("nolictext-lgpl-lib-xyz")
        del entry["licence_text"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("nolictext-lgpl-lib-xyz", proc.stdout + proc.stderr)

    def test_lgpl_missing_source_offer_fails(self):
        """LGPL without a recorded source offer fails (AC-DEPS-014)."""
        entry = lgpl_dynamic_entry("nosource-lgpl-lib-xyz")
        del entry["source_offer"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("nosource-lgpl-lib-xyz", proc.stdout + proc.stderr)

    def test_lgpl_missing_relinkable_objects_fails(self):
        """LGPL without recorded relinkable objects fails (AC-DEPS-014)."""
        entry = lgpl_dynamic_entry("norelink-lgpl-lib-xyz")
        del entry["relinkable_objects"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("norelink-lgpl-lib-xyz", proc.stdout + proc.stderr)

    def test_lgpl_missing_link_fails(self):
        """LGPL with no recorded link field fails (link must be proven dynamic, AC-DEPS-014)."""
        entry = lgpl_dynamic_entry("nolink-lgpl-lib-xyz")
        del entry["link"]
        path = write_manifest([entry])
        try:
            proc = run_cli("--manifest", path)
        finally:
            os.unlink(path)
        self.assertEqual(proc.returncode, 1, msg=proc.stdout + proc.stderr)
        self.assertIn("nolink-lgpl-lib-xyz", proc.stdout + proc.stderr)

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
        """Bundled self-test passes (TC-DEPS-014)."""
        proc = run_cli("--self-test")
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_usage_error_exits_2(self):
        """Unknown flag is a usage error -> exit 2."""
        proc = run_cli("--bogus-flag-xyz")
        self.assertEqual(proc.returncode, 2, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
