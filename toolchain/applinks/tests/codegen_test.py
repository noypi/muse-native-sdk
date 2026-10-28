"""Batch 1 contract tests — FR-NAV-006 / AC-NAV-013 (TC-NAV-013).

Hermetic: stdlib only (unittest, subprocess, tempfile, json); no network,
no secrets, no live device, no Gradle daemon. The codegen CLI under test is
`toolchain/applinks/codegen.py`; this file binds to the T2 `Interface:` line:

  generate(manifestPath: string, outDir: string):
      {intentFiltersXml: string, assetlinksJson: string}
      | CodegenError{reason: DRIFT | SCHEMA_MISMATCH, detail: string}
  CLI `codegen.py --manifest <f> --out <dir> --check` exits 0 on match,
  2 on drift with unified diff on stdout.

Manifest fixture shape follows the T1 contract (hosts, schemes, routes,
packageName, sha256CertFingerprints, autoVerify).

TC-NAV-013 mapping: the spec Check is
`./gradlew :toolchain:appLinksCodegen`; the hermetic equivalent executed
here is `python3 toolchain/applinks/codegen.py --manifest <m> --out <tmp>
--check`. The Gradle task remains the CI owner; this script test covers the
same drift-gate behavior without a daemon.
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
CODEGEN = os.path.join(REPO_ROOT, "toolchain", "applinks", "codegen.py")

TWO_HOST_MANIFEST = {
    "hosts": ["shop.example.com", "social.example.com"],
    "schemes": ["https"],
    "packageName": "com.example.app",
    "sha256CertFingerprints": ["AA:BB:CC:DD"],
    "autoVerify": True,
    "routes": [
        {"host": "shop.example.com", "pathPattern": "/p/.*",
         "routeId": "product", "query": []},
        {"host": "social.example.com", "pathPattern": "/u/.*",
         "routeId": "profile", "query": []},
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


class CodegenContractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.manifest_path = os.path.join(self.tmp.name, "manifest.json")
        with open(self.manifest_path, "w", encoding="utf-8") as fh:
            json.dump(TWO_HOST_MANIFEST, fh)
        self.out_dir = os.path.join(self.tmp.name, "out")
        os.makedirs(self.out_dir, exist_ok=True)

    def tearDown(self):
        self.tmp.cleanup()

    def _generate(self):
        return run_codegen("--manifest", self.manifest_path,
                           "--out", self.out_dir)

    def test_generate_emits_both_artifacts(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        self.assertTrue(os.path.isfile(
            os.path.join(self.out_dir, "intent-filters.xml")))
        self.assertTrue(os.path.isfile(
            os.path.join(self.out_dir, "assetlinks.json")))

    def test_xml_contains_autoverify_and_all_hosts(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        with open(os.path.join(self.out_dir, "intent-filters.xml"),
                  encoding="utf-8") as fh:
            xml = fh.read()
        self.assertIn('android:autoVerify="true"', xml)
        self.assertIn("android.intent.action.VIEW", xml)
        self.assertIn("android.intent.category.BROWSABLE", xml)
        self.assertIn("android.intent.category.DEFAULT", xml)
        for host in TWO_HOST_MANIFEST["hosts"]:
            self.assertIn(host, xml)
        for route in TWO_HOST_MANIFEST["routes"]:
            self.assertIn(route["pathPattern"], xml)

    def test_assetlinks_exact_entry_shape(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        with open(os.path.join(self.out_dir, "assetlinks.json"),
                  encoding="utf-8") as fh:
            entries = json.load(fh)
        self.assertIsInstance(entries, list)
        self.assertEqual(len(entries), 1)
        entry = entries[0]
        self.assertEqual(entry["relation"],
                         ["delegate_permission/common.handle_all_urls"])
        target = entry["target"]
        self.assertEqual(target["namespace"], "android_app")
        self.assertEqual(target["package_name"],
                         TWO_HOST_MANIFEST["packageName"])
        self.assertEqual(target["sha256_cert_fingerprints"],
                         TWO_HOST_MANIFEST["sha256CertFingerprints"])
        self.assertEqual(set(entry.keys()), {"relation", "target"})

    def test_check_passes_when_outputs_match(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        check = run_codegen("--manifest", self.manifest_path,
                            "--out", self.out_dir, "--check")
        self.assertEqual(check.returncode, 0, msg=check.stderr)

    def test_check_exits_2_with_diff_on_hand_edited_assetlinks(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        assetlinks = os.path.join(self.out_dir, "assetlinks.json")
        with open(assetlinks, encoding="utf-8") as fh:
            entries = json.load(fh)
        entries[0]["target"]["sha256_cert_fingerprints"] = ["00:11:22"]
        with open(assetlinks, "w", encoding="utf-8") as fh:
            json.dump(entries, fh, indent=2)
        check = run_codegen("--manifest", self.manifest_path,
                            "--out", self.out_dir, "--check")
        self.assertEqual(check.returncode, 2)
        # Unified diff on stdout names the drifted file.
        self.assertIn("assetlinks.json", check.stdout)
        self.assertIn("@@", check.stdout)

    def test_check_exits_2_with_diff_on_hand_edited_xml(self):
        proc = self._generate()
        self.assertEqual(proc.returncode, 0, msg=proc.stderr)
        xml_path = os.path.join(self.out_dir, "intent-filters.xml")
        with open(xml_path, encoding="utf-8") as fh:
            xml = fh.read()
        with open(xml_path, "w", encoding="utf-8") as fh:
            fh.write(xml.replace("shop.example.com", "other.example.com"))
        check = run_codegen("--manifest", self.manifest_path,
                            "--out", self.out_dir, "--check")
        self.assertEqual(check.returncode, 2)
        self.assertIn("@@", check.stdout)

    def test_schema_mismatch_manifest_fails(self):
        bad = dict(TWO_HOST_MANIFEST)
        bad["sha256CertFingerprints"] = []
        with open(self.manifest_path, "w", encoding="utf-8") as fh:
            json.dump(bad, fh)
        proc = self._generate()
        self.assertNotEqual(proc.returncode, 0)
        combined = proc.stdout + proc.stderr
        self.assertTrue(combined.strip(),
                        msg="failing generate must explain why")


if __name__ == "__main__":
    unittest.main()
