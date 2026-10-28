"""T11 contract tests — FR-NAV-010 / AC-NAV-031 (TC-NAV-031).

Hermetic: stdlib only (unittest, subprocess, tempfile, json, os, sys);
no network, no secrets, no config reads, no live services, no Gradle.
Binds to the plan T11 `Interface:` line (verbatim):

  `python3 toolchain/launch-mode-lint/lint.py --manifest <manifest.json>` (exit 0 clean, exit 2 listing contradictions)

The lint script under test does not exist yet (coder owns it); every
test that invokes it RED-fails until the coder implements the CLI.

Manifest fixture shape (ASSUMPTION — plan names only `launchMode`
(singleTop/singleTask/singleInstance) + `taskAffinity` per graph; the
field spelling below is the test's proposal):
  {"graphs": [{"graphId": str, "launchMode": str, "taskAffinity": str,
               "appLinkTarget": bool}]}
The coder must accept at least this shape; extra manifest fields must be
ignored, not rejected.

Contradiction rules asserted (ASSUMPTIONs — plan gives the
two-singleInstance-shared-affinity example plus the Q-NAV-001 caution):
  R1: two `singleInstance` graphs sharing one `taskAffinity` contradict.
  R2: an App-Link target (`appLinkTarget: true`) inside a `singleInstance`
      graph contradicts.
Clean manifests (distinct affinities, singleTop/singleTask sharing an
affinity) exit 0. Contradictory manifests exit 2 with the offending
graphId(s) named on stdout.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.abspath(__file__)))))
LINT = os.path.join(REPO_ROOT, "toolchain", "launch-mode-lint", "lint.py")

CLEAN_MANIFEST = {
    "graphs": [
        {"graphId": "home", "launchMode": "singleTask",
         "taskAffinity": "app.home", "appLinkTarget": True},
        {"graphId": "settings", "launchMode": "singleTop",
         "taskAffinity": "app.home", "appLinkTarget": False},
        {"graphId": "player", "launchMode": "singleInstance",
         "taskAffinity": "app.player", "appLinkTarget": False},
    ],
}

SHARED_SINGLE_INSTANCE_MANIFEST = {
    "graphs": [
        {"graphId": "player", "launchMode": "singleInstance",
         "taskAffinity": "app.shared", "appLinkTarget": False},
        {"graphId": "call", "launchMode": "singleInstance",
         "taskAffinity": "app.shared", "appLinkTarget": False},
    ],
}

APPLINK_IN_SINGLE_INSTANCE_MANIFEST = {
    "graphs": [
        {"graphId": "home", "launchMode": "singleTask",
         "taskAffinity": "app.home", "appLinkTarget": False},
        {"graphId": "viewer", "launchMode": "singleInstance",
         "taskAffinity": "app.viewer", "appLinkTarget": True},
    ],
}


def run_lint(*args):
    return subprocess.run(
        [sys.executable, LINT, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )


class LaunchModeLintContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.manifest_path = os.path.join(self.tmp.name, "manifest.json")

    def tearDown(self):
        self.tmp.cleanup()

    def _write(self, manifest):
        with open(self.manifest_path, "w", encoding="utf-8") as fh:
            json.dump(manifest, fh)
        return self.manifest_path

    def test_clean_manifest_exits_zero(self):
        proc = run_lint("--manifest",
                        self._write(CLEAN_MANIFEST))
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)

    def test_shared_single_instance_exits_2_naming_graphs(self):
        proc = run_lint("--manifest",
                        self._write(SHARED_SINGLE_INSTANCE_MANIFEST))
        self.assertEqual(proc.returncode, 2, msg=proc.stderr)
        self.assertIn("player", proc.stdout)
        self.assertIn("call", proc.stdout)

    def test_applink_target_in_single_instance_exits_2_naming_graph(self):
        proc = run_lint("--manifest",
                        self._write(APPLINK_IN_SINGLE_INSTANCE_MANIFEST))
        self.assertEqual(proc.returncode, 2, msg=proc.stderr)
        self.assertIn("viewer", proc.stdout)

    def test_single_top_sharing_affinity_is_clean(self):
        manifest = {"graphs": [
            {"graphId": "a", "launchMode": "singleTop",
             "taskAffinity": "app.same", "appLinkTarget": False},
            {"graphId": "b", "launchMode": "singleTop",
             "taskAffinity": "app.same", "appLinkTarget": False},
        ]}
        proc = run_lint("--manifest", self._write(manifest))
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)

    def test_missing_manifest_is_usage_error(self):
        proc = run_lint("--manifest",
                        os.path.join(self.tmp.name, "does-not-exist.json"))
        self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
