"""T5 contract tests — FR-NAV-007 / AC-NAV-020 (TC-NAV-020).

Hermetic: stdlib only (unittest, subprocess, tempfile, json, os, sys);
no network, no secrets, no config reads, no live services, no Gradle.
Binds to the plan T5 `Interface:` line (verbatim):

  `python3 toolchain/route-codegen/codegen.py --manifest <manifest.json> --out <out-dir> [--check]` (exit 0 on match, 2 on drift with unified diff; emits `route_descriptors.h` + `routes.ts`)

The codegen script under test does not exist yet (coder owns it); every
test that invokes it RED-fails until the coder implements the CLI.

Manifest fixture shape (ASSUMPTION — plan names only "one route manifest"
as the single source; the field spelling below is the test's proposal):
  {"routes": [{"routeId": str, "params": [{"name": str, "type": str, "required": bool}]}]}
with type strings "number" | "string" | "boolean". The coder must accept at
least this shape; extra manifest fields must be ignored, not rejected.

A4 proxy note (plan A4): AC-NAV-020's "TypeScript compile error" clause is
verified hermetically by asserting the codegen emits the exact TS type
declaration (e.g. `userId: number`) whose misuse `tsc` would reject, rather
than running `tsc` itself.
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
CODEGEN = os.path.join(REPO_ROOT, "toolchain", "route-codegen", "codegen.py")

PROFILE_MANIFEST = {
    "routes": [
        {"routeId": "profile",
         "params": [{"name": "userId", "type": "number", "required": True}]},
    ],
}


def run_codegen(*args):
    return subprocess.run(
        [sys.executable, CODEGEN, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=120,
    )


class RouteCodegenContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.manifest_path = os.path.join(self.tmp.name, "manifest.json")
        with open(self.manifest_path, "w", encoding="utf-8") as fh:
            json.dump(PROFILE_MANIFEST, fh)
        self.out_dir = os.path.join(self.tmp.name, "out")
        os.makedirs(self.out_dir, exist_ok=True)

    def tearDown(self):
        self.tmp.cleanup()

    def _generate(self):
        return run_codegen("--manifest", self.manifest_path,
                           "--out", self.out_dir)

    def test_generate_exits_zero_and_emits_both_artifacts(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        self.assertTrue(os.path.isfile(
            os.path.join(self.out_dir, "routes.ts")))
        self.assertTrue(os.path.isfile(
            os.path.join(self.out_dir, "route_descriptors.h")))

    def test_routes_ts_emits_userid_number_typing(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        with open(os.path.join(self.out_dir, "routes.ts"),
                  encoding="utf-8") as fh:
            ts = fh.read()
        # Exact declaration: a wrong-typed call (userId: "x") is a tsc
        # error precisely because this line pins `number` (A4 proxy).
        self.assertIn("userId: number", ts)

    def test_header_contains_matching_cpp_descriptor(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        with open(os.path.join(self.out_dir, "route_descriptors.h"),
                  encoding="utf-8") as fh:
            header = fh.read()
        self.assertIn("profile", header)
        self.assertIn("userId", header)

    def test_check_exits_zero_on_match(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        check = run_codegen("--manifest", self.manifest_path,
                            "--out", self.out_dir, "--check")
        self.assertEqual(check.returncode, 0, msg=check.stderr)

    def test_check_exits_2_with_diff_on_drifted_outputs(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        with open(os.path.join(self.out_dir, "routes.ts"),
                  "a", encoding="utf-8") as fh:
            fh.write("\n// hand edit\n")
        check = run_codegen("--manifest", self.manifest_path,
                            "--out", self.out_dir, "--check")
        self.assertEqual(check.returncode, 2, msg=check.stderr)
        combined = check.stdout + check.stderr
        self.assertTrue("---" in combined and "+++" in combined,
                        msg="expected unified diff on drift, got: %r"
                        % combined)


if __name__ == "__main__":
    unittest.main()
