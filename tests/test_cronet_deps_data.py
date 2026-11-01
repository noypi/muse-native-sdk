"""Batch 1 (T1) — Cronet in the shared deps data.

Covers FR-DEPS-010 (AC-DEPS-025) per specs-cpp-dependencies.md:
``get_allowlist()`` names Cronet (owner ``net``, need = default shippable
transport per FR-NET-014) and ``get_cronet_pins()`` returns the FR-DEPS-010
Notes pins verbatim (no invented coordinates; fallback pair stays out per
Q-DEPS-004).

Hermetic: stdlib unittest only, no network/secrets/services/config.
Expected state before implementation: RED — ``get_cronet_pins`` does not
exist and no allow-list row names Cronet, so every test fails
(ImportError on the pins import; assertion failure on the allow-list).
"""

import os
import sys
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

from tools.deps._deps_data import get_allowlist

try:
    from tools.deps._deps_data import get_cronet_pins
except ImportError:  # pragma: no cover — red state: symbol missing
    get_cronet_pins = None

# Pins transcribed from FR-DEPS-010 Notes (specs-cpp-dependencies.md).
BUNDLED_COORD = "org.chromium.net:cronet-bundled:500.1.0"
BUNDLED_URL = (
    "https://maven.google.com/org/chromium/net/cronet-bundled/500.1.0/"
    "cronet-bundled-500.1.0.aar"
)
BUNDLED_SHA256 = (
    "fa286aeebe66dcab4a64df017d566cb8ec563c3bb63af527997c3a4e056bf295"
)
API_COORD = "org.chromium.net:cronet:500.1.0"
API_URL = "https://maven.google.com/org/chromium/net/cronet/500.1.0/cronet-500.1.0.aar"
API_SHA256 = (
    "d0cbb75a3e558166a1e21747d75b788b2650ddf8bec321b727b0878f01addcf8"
)
LICENCE = "BSD-3-Clause"
# Notes record `chromium.googlesource.com/chromium/src/+/HEAD/LICENSE`
# (ASSUMPTION: `https://` scheme prefix; the spec text omits the scheme).
LICENCE_URL = "https://chromium.googlesource.com/chromium/src/+/HEAD/LICENSE"


def normalise_pins(pins):
    """Normalise the pins container to a list of pin dicts.

    Accepts a list of pin dicts, a dict keyed by Maven coordinate holding
    pin dicts, or a single pin dict. Anything else raises AssertionError.
    """
    if isinstance(pins, dict) and "version" in pins:
        return [pins]
    if isinstance(pins, dict):
        return list(pins.values())
    if isinstance(pins, (list, tuple)):
        return list(pins)
    raise AssertionError("pins container must be a dict or a list, got %r" % type(pins))


class TestCronetAllowlist(unittest.TestCase):
    """T1 allow-list row: Cronet admitted with owner/need per AC-DEPS-025."""

    def test_allowlist_contains_cronet_owned_by_net(self):
        """`get_allowlist()` contains `name == "Cronet"`, `owner == "net"`."""
        entries = get_allowlist()
        self.assertTrue(
            any(
                isinstance(e, dict)
                and e.get("name") == "Cronet"
                and e.get("owner") == "net"
                for e in entries
            ),
            "allow-list must contain a Cronet entry owned by net",
        )

    def test_cronet_need_names_default_shippable_transport(self):
        """Cronet `need` names the default shippable transport per FR-NET-014."""
        entries = get_allowlist()
        cronet = [e for e in entries if isinstance(e, dict) and e.get("name") == "Cronet"]
        self.assertTrue(cronet, "allow-list must contain a Cronet entry")
        need = cronet[0].get("need", "")
        self.assertIn("shippable transport", need)
        self.assertIn("FR-NET-014", need)


class TestCronetPins(unittest.TestCase):
    """T1 pins: `get_cronet_pins()` returns the FR-DEPS-010 Notes pins verbatim."""

    def test_get_cronet_pins_exists(self):
        """The `get_cronet_pins` symbol exists (red when T1 is unimplemented)."""
        self.assertIsNotNone(get_cronet_pins, "tools.deps._deps_data.get_cronet_pins is missing")

    def test_pins_cover_both_artifacts(self):
        """Pins cover both `cronet-bundled:500.1.0` and `cronet:500.1.0`."""
        pins = normalise_pins(get_cronet_pins())
        text = " ".join(str(p) for p in pins)
        self.assertIn(BUNDLED_COORD, text)
        self.assertIn(API_COORD, text)

    def test_bundled_pin_verbatim(self):
        """Bundled AAR pin matches the Notes version/sha256/URL verbatim."""
        pins = normalise_pins(get_cronet_pins())
        bundled = [p for p in pins if isinstance(p, dict) and BUNDLED_COORD in str(p)]
        self.assertTrue(bundled, "no pin entry for %s" % BUNDLED_COORD)
        pin = bundled[0]
        self.assertEqual(pin.get("version"), "500.1.0")
        self.assertEqual(pin.get("sha256"), BUNDLED_SHA256)
        self.assertEqual(pin.get("url"), BUNDLED_URL)

    def test_api_pin_verbatim(self):
        """API AAR pin matches the Notes version/sha256/URL verbatim."""
        pins = normalise_pins(get_cronet_pins())
        api = [p for p in pins if isinstance(p, dict) and API_COORD in str(p) and "bundled" not in str(p.get("url", "") + str(p))]
        if not api:
            # Fall back: any pin entry that is not the bundled one.
            api = [
                p
                for p in pins
                if isinstance(p, dict) and BUNDLED_COORD not in str(p)
            ]
        self.assertTrue(api, "no pin entry for %s" % API_COORD)
        pin = api[0]
        self.assertEqual(pin.get("version"), "500.1.0")
        self.assertEqual(pin.get("sha256"), API_SHA256)
        self.assertEqual(pin.get("url"), API_URL)

    def test_pins_licence_bsd3_with_googlesource_url(self):
        """Every Cronet pin records BSD-3-Clause plus the googlesource licence URL."""
        pins = normalise_pins(get_cronet_pins())
        self.assertTrue(pins, "pins must not be empty")
        for pin in pins:
            self.assertIsInstance(pin, dict)
            self.assertEqual(pin.get("licence"), LICENCE)
            self.assertEqual(pin.get("licence_url"), LICENCE_URL)

    def test_pins_invent_no_fallback_coordinates(self):
        """Pins contain no BoringSSL/nghttp2 fallback coordinates (Q-DEPS-004 open)."""
        text = " ".join(str(p) for p in normalise_pins(get_cronet_pins())).lower()
        self.assertNotIn("boringssl", text)
        self.assertNotIn("nghttp2", text)


if __name__ == "__main__":
    unittest.main()
