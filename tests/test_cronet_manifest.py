"""Batch 1 (T2) — Cronet in the dependency manifest.

Covers FR-DEPS-010 (AC-DEPS-025), file-side TC-DEPS-026
(`Cronet in build/dependency-manifest.json`): the manifest holds one
Cronet entry — owner `net`, a transport-seam interface, a
BoringSSL+nghttp2 fallback exit plan, BSD-3-Clause licence — matching the
existing entry shape (name/owner/interface/exit_plan/licence/link).

Hermetic: stdlib unittest only; reads the committed manifest read-only,
no network/secrets/services/config.
Expected state before implementation: RED — the manifest holds no Cronet
entry, so every entry test fails (assertion failure, not a test defect).
"""

import json
import os
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(REPO_ROOT, "build", "dependency-manifest.json")


def load_entries():
    """Load the manifest entry list from the committed data file."""
    with open(MANIFEST, "r", encoding="utf-8") as handle:
        data = json.load(handle)
    if isinstance(data, dict):
        data = data["dependencies"]
    return data


def cronet_entries():
    """Return manifest entries named Cronet (empty before T2 lands)."""
    return [
        e for e in load_entries() if isinstance(e, dict) and e.get("name") == "Cronet"
    ]


class TestCronetManifest(unittest.TestCase):
    """T2 data file: manifest entry for Cronet per AC-DEPS-025."""

    def test_manifest_is_parseable(self):
        """The manifest file parses to a list of entry dicts (precondition)."""
        entries = load_entries()
        self.assertIsInstance(entries, list)
        self.assertTrue(entries, "manifest must not be empty")
        for entry in entries:
            self.assertIsInstance(entry, dict)

    def test_manifest_contains_cronet(self):
        """`Cronet in build/dependency-manifest.json` (TC-DEPS-026 file-side)."""
        self.assertTrue(cronet_entries(), "manifest must contain a Cronet entry")

    def test_cronet_owner_is_net(self):
        """Cronet entry owner is `net`."""
        entries = cronet_entries()
        self.assertTrue(entries, "manifest must contain a Cronet entry")
        self.assertEqual(entries[0].get("owner"), "net")

    def test_cronet_interface_is_transport_seam(self):
        """Cronet entry names a non-empty transport-seam interface string."""
        # ASSUMPTION: the exact seam string is settled by the T4 seam_check
        # inventory; here we require a non-empty interface naming transport.
        entries = cronet_entries()
        self.assertTrue(entries, "manifest must contain a Cronet entry")
        interface = entries[0].get("interface", "")
        self.assertIsInstance(interface, str)
        self.assertTrue(interface, "Cronet entry must name an isolating interface")
        self.assertIn("ransport", interface)

    def test_cronet_exit_plan_names_boringssl_nghttp2_fallback(self):
        """Cronet exit plan falls back to BoringSSL+nghttp2 (text only, Q-DEPS-004)."""
        entries = cronet_entries()
        self.assertTrue(entries, "manifest must contain a Cronet entry")
        exit_plan = entries[0].get("exit_plan", "")
        self.assertIn("BoringSSL", exit_plan)
        self.assertIn("nghttp2", exit_plan)

    def test_cronet_licence_is_bsd3(self):
        """Cronet entry licence is `BSD-3-Clause`."""
        entries = cronet_entries()
        self.assertTrue(entries, "manifest must contain a Cronet entry")
        self.assertEqual(entries[0].get("licence"), "BSD-3-Clause")

    def test_cronet_entry_matches_existing_shape(self):
        """Cronet entry carries the same required fields as every entry."""
        entries = cronet_entries()
        self.assertTrue(entries, "manifest must contain a Cronet entry")
        for field in ("name", "owner", "interface", "exit_plan", "licence", "link"):
            self.assertIn(field, entries[0], "Cronet entry missing field %r" % field)


if __name__ == "__main__":
    unittest.main()
