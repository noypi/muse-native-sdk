"""Batch 3 (T5+T6) — golden coverage + frozen fonts (FR-TESTING-004).

Covers FR-TESTING-004 / AC-TESTING-032 / TC-TESTING-032
(``./gradlew :testing:goldenCoverage``) + AC-TESTING-033 / TC-TESTING-033
(grep ``frozen in testing/goldens/fonts/manifest.json``):

- T5: ``golden_coverage(matrix_path: str) -> dict`` returns
  ``{components: int, variants: list[str], missing: list[str],
  goldens: list[str]}``; ``render_golden(component: str, variant: str,
  driver_class: str) -> dict`` returns ``{png_path: str, variant: str,
  driver_class: str}``. Coverage reads ``engine/layout/m3_matrix.json``
  and requires each component x {light, dark, rtl} golden;
  ``render_golden`` delegates to ``testing.cli.snapshot.render_snapshot``
  with variant+driver_class pinned via the ``testing.determinism``
  pinned env; goldens are partitioned by driver class.
- T6: ``assert_frozen_fonts(manifest_path: str) -> dict`` returns
  ``{frozen: bool, fonts: list[str], digest: str}``; raises
  ``AssertionError`` naming the offending font on any host-fallback or
  manifest mismatch. The manifest carries a ``"frozen": true`` field.

Public boundaries under test: ``testing.goldens.coverage.golden_coverage``,
``testing.goldens.coverage.render_golden``,
``testing.goldens.fonts.assert_frozen_fonts``, plus
``testing/goldens/fonts/manifest.json`` (TC-TESTING-033 grep surface).

Hermetic: stdlib unittest only (plus ``unittest.mock`` fakes at the
snapshot/filesystem boundary), tmp dirs and inline fixtures, no
network/downloads/device/secrets, no wall-clock or socket reads in the
tests, no dependence on host fonts, locale, or timezone.

Expected state before implementation: RED — ``testing/goldens`` does
not exist, so every test errors (ModuleNotFoundError) or fails the
file-existence checks.
"""

import hashlib
import json
import os
import sys
import unittest
from unittest import mock

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)

GOLDENS_DIR = os.path.join(REPO_ROOT, "testing", "goldens")
GOLDENS_INIT = os.path.join(GOLDENS_DIR, "__init__.py")
COVERAGE_PY = os.path.join(GOLDENS_DIR, "coverage.py")
FONTS_PY = os.path.join(GOLDENS_DIR, "fonts.py")
FONTS_DIR = os.path.join(GOLDENS_DIR, "fonts")
FONT_MANIFEST = os.path.join(FONTS_DIR, "manifest.json")
MATRIX_PATH = os.path.join(REPO_ROOT, "engine", "layout", "m3_matrix.json")

EXPECTED_VARIANTS = ("light", "dark", "rtl")

# ASSUMPTION: one golden entry per component x variant; the driver class
# partitions at render time (it is echoed in ``render_golden`` results
# and in distinct ``png_path`` values, not multiplied into coverage).
# ASSUMPTION: T6 manifest digest scheme is the sha256 hex of
# ``"\\n".join(f"{file}:{version}:{sha256}"`` sorted by file. The tests
# publish this scheme; the coder implements to it.


def _matrix_components(path=MATRIX_PATH):
    with open(path, "r", encoding="utf-8") as handle:
        return json.load(handle)["components"]


def _write_matrix(directory, names):
    path = os.path.join(directory, "m3_matrix.json")
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(
            {
                "schemaVersion": 1,
                "components": [{"name": name} for name in names],
            },
            handle,
        )
    return path


def _font_bytes(name):
    return b"frozen-font-bytes\x00" + name.encode("utf-8")


def _write_font_manifest(directory, entries, frozen=True, digest=None):
    """Write a T6 font manifest plus its font files into ``directory``.

    ``entries`` maps file name -> version. Returns the manifest path.
    """
    fonts = []
    for name in sorted(entries):
        payload = _font_bytes(name)
        with open(os.path.join(directory, name), "wb") as handle:
            handle.write(payload)
        fonts.append(
            {
                "file": name,
                "version": entries[name],
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )
    if digest is None:
        digest = hashlib.sha256(
            "\n".join(
                "%s:%s:%s" % (f["file"], f["version"], f["sha256"])
                for f in fonts
            ).encode("utf-8")
        ).hexdigest()
    path = os.path.join(directory, "manifest.json")
    manifest = {"frozen": frozen, "fonts": fonts, "digest": digest}
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle)
    return path


class TestGoldenFilesExist(unittest.TestCase):
    def test_goldens_package_init_exists(self):
        self.assertTrue(
            os.path.isfile(GOLDENS_INIT),
            "testing/goldens/__init__.py missing (T5)",
        )

    def test_coverage_module_exists(self):
        self.assertTrue(
            os.path.isfile(COVERAGE_PY),
            "testing/goldens/coverage.py missing (T5)",
        )

    def test_fonts_module_exists(self):
        self.assertTrue(
            os.path.isfile(FONTS_PY),
            "testing/goldens/fonts.py missing (T6)",
        )

    def test_font_manifest_exists_and_is_json(self):
        self.assertTrue(
            os.path.isfile(FONT_MANIFEST),
            "testing/goldens/fonts/manifest.json missing (T6)",
        )
        with open(FONT_MANIFEST, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
        self.assertIsInstance(manifest, dict)

    def test_font_manifest_carries_frozen_true_for_tc033_grep(self):
        with open(FONT_MANIFEST, "r", encoding="utf-8") as handle:
            raw = handle.read()
        self.assertIn('"frozen"', raw)
        manifest = json.loads(raw)
        self.assertTrue(
            manifest.get("frozen") is True,
            'manifest must carry "frozen": true (TC-TESTING-033)',
        )


class TestGoldenCoverage(unittest.TestCase):
    def _coverage(self, matrix_path):
        from testing.goldens.coverage import golden_coverage

        return golden_coverage(matrix_path)

    def test_coverage_reports_components_variants_and_empty_missing(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            matrix = _write_matrix(tmp, ["Button", "Card"])
            with mock.patch("os.path.isfile", return_value=True), mock.patch(
                "os.path.exists", return_value=True
            ):
                result = self._coverage(matrix)
        self.assertEqual(result["components"], 2)
        self.assertEqual(set(result["variants"]), set(EXPECTED_VARIANTS))
        self.assertEqual(result["missing"], [])
        self.assertEqual(
            len(result["goldens"]) + len(result["missing"]),
            result["components"] * len(EXPECTED_VARIANTS),
        )

    def test_coverage_missing_golden_fails_naming_it(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            matrix = _write_matrix(tmp, ["Button", "Card"])

            def _exists(path):
                return "Card-dark" not in str(path)

            with mock.patch("os.path.isfile", side_effect=_exists), mock.patch(
                "os.path.exists", side_effect=_exists
            ):
                result = self._coverage(matrix)
        self.assertTrue(
            result["missing"],
            "removing one golden must fail the gate (AC-TESTING-032)",
        )
        self.assertTrue(
            any("Card" in entry for entry in result["missing"]),
            "missing list must name the removed golden, got %r"
            % (result["missing"],),
        )

    def test_coverage_matches_real_m3_matrix_component_count(self):
        expected = len(_matrix_components())
        self.assertGreaterEqual(expected, 25)
        with mock.patch("os.path.isfile", return_value=True), mock.patch(
            "os.path.exists", return_value=True
        ):
            result = self._coverage(MATRIX_PATH)
        self.assertEqual(result["components"], expected)
        self.assertEqual(set(result["variants"]), set(EXPECTED_VARIANTS))
        self.assertEqual(result["missing"], [])

    def test_coverage_unknown_matrix_path_raises(self):
        with self.assertRaises((FileNotFoundError, ValueError, OSError)):
            self._coverage(os.path.join(REPO_ROOT, "no-such-matrix.json"))

    def test_coverage_result_shapes(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            matrix = _write_matrix(tmp, ["Button"])
            with mock.patch("os.path.isfile", return_value=True), mock.patch(
                "os.path.exists", return_value=True
            ):
                result = self._coverage(matrix)
        self.assertIsInstance(result["components"], int)
        self.assertIsInstance(result["variants"], list)
        self.assertIsInstance(result["missing"], list)
        self.assertIsInstance(result["goldens"], list)


class TestRenderGolden(unittest.TestCase):
    def _render(self, component, variant, driver_class):
        from testing.goldens.coverage import render_golden

        return render_golden(component, variant, driver_class)

    def test_render_golden_returns_png_path_variant_driver(self):
        result = self._render("Button", "light", "mali")
        self.assertEqual(result["variant"], "light")
        self.assertEqual(result["driver_class"], "mali")
        self.assertTrue(result["png_path"].endswith(".png"))
        self.assertTrue(os.path.isfile(result["png_path"]))
        with open(result["png_path"], "rb") as handle:
            self.assertTrue(
                handle.read(8).startswith(b"\x89PNG"),
                "rendered golden must be a PNG",
            )

    def test_render_golden_rerun_is_byte_identical(self):
        first = self._render("Card", "dark", "adreno")
        second = self._render("Card", "dark", "adreno")
        with open(first["png_path"], "rb") as handle:
            first_bytes = handle.read()
        with open(second["png_path"], "rb") as handle:
            second_bytes = handle.read()
        self.assertEqual(first_bytes, second_bytes)

    def test_render_golden_partitions_by_driver_class(self):
        mali = self._render("Card", "rtl", "mali")
        adreno = self._render("Card", "rtl", "adreno")
        self.assertNotEqual(mali["png_path"], adreno["png_path"])

    def test_render_golden_covers_all_variants(self):
        for variant in EXPECTED_VARIANTS:
            result = self._render("Dialog", variant, "powervr")
            self.assertEqual(result["variant"], variant)
            self.assertTrue(os.path.isfile(result["png_path"]))

    def test_render_golden_unknown_variant_raises(self):
        with self.assertRaises(ValueError):
            self._render("Button", "neon", "mali")

    def test_render_golden_empty_component_raises(self):
        with self.assertRaises(ValueError):
            self._render("", "light", "mali")

    def test_render_golden_path_traversal_raises(self):
        with self.assertRaises(ValueError):
            self._render("../evil", "light", "mali")

    def test_render_golden_delegates_to_snapshot_under_pinned_env(self):
        import testing.goldens.coverage as coverage_mod
        from testing.cli import snapshot as snapshot_mod

        target = (
            coverage_mod
            if hasattr(coverage_mod, "render_snapshot")
            else snapshot_mod
        )
        seen = {}
        real_render = snapshot_mod.render_snapshot

        def _spy(component, variant):
            seen["env"] = dict(os.environ)
            return real_render(component, variant)

        with mock.patch.object(
            target, "render_snapshot", side_effect=_spy
        ):
            result = self._render("Chip", "dark", "mali")
        self.assertIn("LC_ALL", seen.get("env", {}))
        self.assertIn("TZ", seen.get("env", {}))
        self.assertTrue(result["png_path"])

    def test_render_golden_restores_host_environment(self):
        before = dict(os.environ)
        self._render("Chip", "light", "mali")
        self.assertEqual(dict(os.environ), before)


class TestFrozenFonts(unittest.TestCase):
    def _assert_fonts(self, manifest_path):
        from testing.goldens.fonts import assert_frozen_fonts

        return assert_frozen_fonts(manifest_path)

    def test_assert_frozen_fonts_passes_on_valid_manifest(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(
                tmp, {"NotoSans.ttf": "1.0", "Roboto.ttf": "2.1"}
            )
            result = self._assert_fonts(manifest)
        self.assertTrue(result["frozen"])
        self.assertEqual(
            sorted(result["fonts"]), ["NotoSans.ttf", "Roboto.ttf"]
        )
        self.assertTrue(result["digest"])

    def test_assert_frozen_fonts_digest_is_deterministic(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(tmp, {"NotoSans.ttf": "1.0"})
            first = self._assert_fonts(manifest)
            second = self._assert_fonts(manifest)
        self.assertEqual(first["digest"], second["digest"])

    def test_assert_frozen_fonts_fails_naming_removed_font(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(
                tmp, {"NotoSans.ttf": "1.0", "Roboto.ttf": "2.1"}
            )
            with open(manifest, "r", encoding="utf-8") as handle:
                tampered = json.load(handle)
            removed = tampered["fonts"].pop(1)
            # Digest is now stale: entry removed without re-pinning.
            with open(manifest, "w", encoding="utf-8") as handle:
                json.dump(tampered, handle)
            with self.assertRaises(AssertionError) as ctx:
                self._assert_fonts(manifest)
        self.assertIn(removed["file"], str(ctx.exception))

    def test_assert_frozen_fonts_fails_naming_tampered_font(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(tmp, {"NotoSans.ttf": "1.0"})
            with open(os.path.join(tmp, "NotoSans.ttf"), "wb") as handle:
                handle.write(b"attacker-controlled bytes")
            with self.assertRaises(AssertionError) as ctx:
                self._assert_fonts(manifest)
        self.assertIn("NotoSans.ttf", str(ctx.exception))

    def test_assert_frozen_fonts_rejects_host_fallback_path(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(tmp, {"NotoSans.ttf": "1.0"})
            with open(manifest, "r", encoding="utf-8") as handle:
                tampered = json.load(handle)
            tampered["fonts"].append(
                {
                    "file": "/usr/share/fonts/NotoSans-Host.ttf",
                    "version": "host",
                    "sha256": "0" * 64,
                }
            )
            with open(manifest, "w", encoding="utf-8") as handle:
                json.dump(tampered, handle)
            with self.assertRaises(AssertionError) as ctx:
                self._assert_fonts(manifest)
        self.assertIn("NotoSans-Host.ttf", str(ctx.exception))

    def test_assert_frozen_fonts_rejects_missing_font_file(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(tmp, {"NotoSans.ttf": "1.0"})
            os.remove(os.path.join(tmp, "NotoSans.ttf"))
            with self.assertRaises(AssertionError) as ctx:
                self._assert_fonts(manifest)
        self.assertIn("NotoSans.ttf", str(ctx.exception))

    def test_assert_frozen_fonts_rejects_unfrozen_manifest(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = _write_font_manifest(
                tmp, {"NotoSans.ttf": "1.0"}, frozen=False
            )
            with self.assertRaises(AssertionError):
                self._assert_fonts(manifest)

    def test_assert_frozen_fonts_rejects_malformed_manifest(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            manifest = os.path.join(tmp, "manifest.json")
            with open(manifest, "w", encoding="utf-8") as handle:
                handle.write("{not-json")
            with self.assertRaises((AssertionError, ValueError)):
                self._assert_fonts(manifest)

    def test_shipped_manifest_shape_and_pass(self):
        with open(FONT_MANIFEST, "r", encoding="utf-8") as handle:
            manifest = json.load(handle)
        self.assertTrue(manifest.get("frozen") is True)
        self.assertTrue(manifest.get("fonts"))
        for entry in manifest["fonts"]:
            for key in ("file", "version", "sha256"):
                self.assertIn(key, entry)
                self.assertTrue(entry[key])
        self.assertTrue(manifest.get("digest"))
        result = self._assert_fonts(FONT_MANIFEST)
        self.assertTrue(result["frozen"])


if __name__ == "__main__":
    unittest.main()
