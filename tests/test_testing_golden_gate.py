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


TOLERANCES_JSON = os.path.join(GOLDENS_DIR, "tolerances.json")
TOLERANCE_PY = os.path.join(GOLDENS_DIR, "tolerance.py")
DIFF_PY = os.path.join(GOLDENS_DIR, "diff.py")
GRADLEW_PATH = os.path.join(REPO_ROOT, "gradlew")

# ASSUMPTION: ``tolerances.json`` maps golden test id -> tolerance entry
# plus an ``audit`` list that ``set_tolerance`` appends to; entry ids for
# shipped goldens follow the ``<component>-<variant>`` golden naming used
# by ``coverage._golden_path``. The tests publish this shape; the coder
# implements to it.
TOLERANCE_KEYS = ("mean_per_channel_error", "max_differing_pixels")


def _load_tolerances_raw():
    with open(TOLERANCES_JSON, "r", encoding="utf-8") as handle:
        return json.load(handle)


def _tolerance_entries(raw):
    if isinstance(raw, dict) and isinstance(raw.get("tolerances"), dict):
        return raw["tolerances"]
    if isinstance(raw, dict):
        return {
            key: value
            for key, value in raw.items()
            if isinstance(value, dict) and "audit" not in key
        }
    return {}


def _write_rgba_png(path, width, height, rows):
    """Write an unfiltered 8-bit RGBA PNG using stdlib only."""
    import struct
    import zlib

    def _chunk(kind, data):
        header = struct.pack(">I", len(data)) + kind + data
        return header + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    raw = b"".join(
        b"\x00"
        + b"".join(
            struct.pack("BBBB", *pixel) for pixel in rows[y]
        )
        for y in range(height)
    )
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", ihdr)
        + _chunk(b"IDAT", zlib.compress(raw))
        + _chunk(b"IEND", b"")
    )
    with open(path, "wb") as handle:
        handle.write(png)
    return path


def _solid_rows(width, height, pixel):
    return [[tuple(pixel) for _ in range(width)] for _ in range(height)]


class TestToleranceFilesExist(unittest.TestCase):
    def test_tolerances_json_exists_and_is_json(self):
        self.assertTrue(
            os.path.isfile(TOLERANCES_JSON),
            "missing %s" % TOLERANCES_JSON,
        )
        with open(TOLERANCES_JSON, "r", encoding="utf-8") as handle:
            raw = json.load(handle)
        self.assertIsInstance(raw, dict)

    def test_tolerance_module_exists(self):
        self.assertTrue(
            os.path.isfile(TOLERANCE_PY),
            "missing %s" % TOLERANCE_PY,
        )

    def test_diff_module_exists(self):
        self.assertTrue(
            os.path.isfile(DIFF_PY),
            "missing %s" % DIFF_PY,
        )


class TestGetTolerance(unittest.TestCase):
    def _get_tolerance(self, test_id):
        from testing.goldens.tolerance import get_tolerance

        return get_tolerance(test_id)

    def _first_shipped_id(self):
        entries = _tolerance_entries(_load_tolerances_raw())
        self.assertTrue(entries, "tolerances.json carries no entries")
        return sorted(entries)[0]

    def test_get_tolerance_returns_mean_error_and_pixel_bound(self):
        # Plan Detail (quoted verbatim): "every golden test id has an
        # explicit entry (no global default silently applied — missing
        # entry raises `KeyError`)".
        result = self._get_tolerance(self._first_shipped_id())
        self.assertIsInstance(result, dict)
        for key in TOLERANCE_KEYS:
            self.assertIn(key, result)
        self.assertIsInstance(
            result["mean_per_channel_error"], (int, float)
        )
        self.assertIsInstance(result["max_differing_pixels"], int)
        self.assertGreaterEqual(result["mean_per_channel_error"], 0)
        self.assertGreaterEqual(result["max_differing_pixels"], 0)

    def test_get_tolerance_missing_entry_raises_key_error(self):
        with self.assertRaises(KeyError):
            self._get_tolerance("no-such-golden-test-xyz")

    def test_get_tolerance_has_no_silent_default(self):
        # A second unknown id must also raise: no global default may be
        # silently applied for any test id.
        with self.assertRaises(KeyError):
            self._get_tolerance("another-unknown-golden-test-xyz")

    def test_every_shipped_entry_declares_both_metrics(self):
        entries = _tolerance_entries(_load_tolerances_raw())
        self.assertTrue(entries, "tolerances.json carries no entries")
        for test_id, entry in entries.items():
            for key in TOLERANCE_KEYS:
                self.assertIn(
                    key, entry, "tolerance %r missing %r" % (test_id, key)
                )

    def test_get_tolerance_empty_test_id_raises(self):
        # Security/input validation: an empty test id must not resolve
        # to a tolerance entry.
        # (ASSUMPTION: empty test id raises KeyError, never a default.)
        with self.assertRaises(KeyError):
            self._get_tolerance("")


class TestSetTolerance(unittest.TestCase):
    def _backup_and_restore(self):
        with open(TOLERANCES_JSON, "rb") as handle:
            before = handle.read()
        self.addCleanup(self._restore, before)

    def _restore(self, before):
        with open(TOLERANCES_JSON, "wb") as handle:
            handle.write(before)

    def test_set_tolerance_empty_review_ref_rejected(self):
        # Plan Detail (quoted verbatim): "`set_tolerance` requires a
        # non-empty review ref (e.g. PR/issue id)".
        from testing.goldens.tolerance import set_tolerance

        with self.assertRaises(PermissionError):
            set_tolerance(
                "unittest-tolerance-gate",
                {
                    "mean_per_channel_error": 0.0,
                    "max_differing_pixels": 0,
                },
                "",
            )

    def test_set_tolerance_none_review_ref_rejected(self):
        # Security: a missing review ref must not open a bypass that
        # writes tolerances without review.
        from testing.goldens.tolerance import set_tolerance

        with self.assertRaises((PermissionError, TypeError)):
            set_tolerance(
                "unittest-tolerance-gate",
                {
                    "mean_per_channel_error": 0.0,
                    "max_differing_pixels": 0,
                },
                None,
            )

    def test_set_tolerance_with_review_ref_appends_audit_log(self):
        # Plan Detail (quoted verbatim): "appends to `tolerances.json`
        # audit log".
        from testing.goldens.tolerance import get_tolerance, set_tolerance

        self._backup_and_restore()
        test_id = "unittest-tolerance-synthetic"
        result = set_tolerance(
            test_id,
            {"mean_per_channel_error": 1.5, "max_differing_pixels": 4},
            "PR-1234",
        )
        self.assertIsInstance(result, dict)
        stored = get_tolerance(test_id)
        self.assertEqual(stored["mean_per_channel_error"], 1.5)
        self.assertEqual(stored["max_differing_pixels"], 4)
        with open(TOLERANCES_JSON, "r", encoding="utf-8") as handle:
            text = handle.read()
        self.assertIn("PR-1234", text)

    def test_set_tolerance_does_not_mutate_on_rejected_review(self):
        from testing.goldens.tolerance import set_tolerance

        self._backup_and_restore()
        with open(TOLERANCES_JSON, "rb") as handle:
            before = handle.read()
        with self.assertRaises(PermissionError):
            set_tolerance(
                "unittest-tolerance-rejected",
                {"mean_per_channel_error": 9.0, "max_differing_pixels": 9},
                "",
            )
        with open(TOLERANCES_JSON, "rb") as handle:
            self.assertEqual(handle.read(), before)

    def test_set_tolerance_traversal_test_id_rejected(self):
        # Security: a traversal test id must not escape the tolerances
        # store or be accepted as a review-gated change.
        from testing.goldens.tolerance import set_tolerance

        with self.assertRaises((ValueError, KeyError, PermissionError)):
            set_tolerance(
                "../../tmp-evil-tolerance",
                {"mean_per_channel_error": 0.0, "max_differing_pixels": 0},
                "PR-1234",
            )


class TestPixelDiff(unittest.TestCase):
    def _pixel_diff(self, a_path, b_path, tolerance):
        from testing.goldens.diff import pixel_diff

        return pixel_diff(a_path, b_path, tolerance)

    def _strict(self):
        return {"mean_per_channel_error": 0.0, "max_differing_pixels": 0}

    def test_pixel_diff_identical_rerun_is_zero_diff(self):
        # Plan Acceptance (quoted verbatim): "identical rerun produces
        # zero diffs".
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            rows = _solid_rows(8, 8, (10, 20, 30, 255))
            a_path = _write_rgba_png(os.path.join(tmp, "a.png"), 8, 8, rows)
            b_path = _write_rgba_png(os.path.join(tmp, "b.png"), 8, 8, rows)
            result = self._pixel_diff(a_path, b_path, self._strict())
        self.assertIsInstance(result, dict)
        for key in ("flagged", "differing_pixels", "mean_error"):
            self.assertIn(key, result)
        self.assertFalse(result["flagged"])
        self.assertEqual(result["differing_pixels"], 0)
        self.assertEqual(result["mean_error"], 0.0)

    def test_pixel_diff_one_px_glyph_shift_flagged(self):
        # Plan Detail (quoted verbatim): "sensitivity proven by synthetic
        # fixtures (1px-shifted glyph strip ... flagged at T7
        # tolerances)".
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            base = _solid_rows(8, 8, (255, 255, 255, 255))
            for y in range(8):
                base[y][3] = (0, 0, 0, 255)
            shifted = [list(row) for row in base]
            for y in range(8):
                shifted[y] = [shifted[y][-1]] + shifted[y][:-1]
            a_path = _write_rgba_png(os.path.join(tmp, "a.png"), 8, 8, base)
            b_path = _write_rgba_png(
                os.path.join(tmp, "b.png"), 8, 8, shifted
            )
            result = self._pixel_diff(a_path, b_path, self._strict())
        self.assertTrue(result["flagged"])
        self.assertGreater(result["differing_pixels"], 0)
        self.assertGreater(result["mean_error"], 0.0)

    def test_pixel_diff_blend_change_flagged(self):
        # Plan Detail (quoted verbatim): "blend-mode-altered swatch ...
        # flagged at T7 tolerances".
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            a_rows = _solid_rows(4, 4, (200, 100, 50, 255))
            b_rows = _solid_rows(4, 4, (50, 100, 200, 255))
            a_path = _write_rgba_png(
                os.path.join(tmp, "a.png"), 4, 4, a_rows
            )
            b_path = _write_rgba_png(
                os.path.join(tmp, "b.png"), 4, 4, b_rows
            )
            result = self._pixel_diff(a_path, b_path, self._strict())
        self.assertTrue(result["flagged"])
        self.assertEqual(result["differing_pixels"], 16)
        self.assertGreater(result["mean_error"], 0.0)

    def test_pixel_diff_within_tolerance_not_flagged(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            a_rows = _solid_rows(4, 4, (100, 100, 100, 255))
            b_rows = _solid_rows(4, 4, (100, 100, 100, 255))
            b_rows[0][0] = (101, 100, 100, 255)
            a_path = _write_rgba_png(
                os.path.join(tmp, "a.png"), 4, 4, a_rows
            )
            b_path = _write_rgba_png(
                os.path.join(tmp, "b.png"), 4, 4, b_rows
            )
            loose = {
                "mean_per_channel_error": 5.0,
                "max_differing_pixels": 4,
            }
            result = self._pixel_diff(a_path, b_path, loose)
        self.assertFalse(result["flagged"])
        self.assertEqual(result["differing_pixels"], 1)

    def test_pixel_diff_dimension_mismatch_flagged(self):
        # (ASSUMPTION: images of different sizes are flagged with
        # differing_pixels > 0 rather than raising.)
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            a_path = _write_rgba_png(
                os.path.join(tmp, "a.png"),
                4,
                4,
                _solid_rows(4, 4, (1, 2, 3, 255)),
            )
            b_path = _write_rgba_png(
                os.path.join(tmp, "b.png"),
                2,
                2,
                _solid_rows(2, 2, (1, 2, 3, 255)),
            )
            result = self._pixel_diff(a_path, b_path, self._strict())
        self.assertTrue(result["flagged"])
        self.assertGreater(result["differing_pixels"], 0)

    def test_pixel_diff_missing_file_raises(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            a_path = _write_rgba_png(
                os.path.join(tmp, "a.png"),
                2,
                2,
                _solid_rows(2, 2, (1, 2, 3, 255)),
            )
            with self.assertRaises((FileNotFoundError, OSError, ValueError)):
                self._pixel_diff(
                    a_path, os.path.join(tmp, "absent.png"), self._strict()
                )

    def test_pixel_diff_corrupt_png_raises(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            good = _write_rgba_png(
                os.path.join(tmp, "good.png"),
                2,
                2,
                _solid_rows(2, 2, (1, 2, 3, 255)),
            )
            bad = os.path.join(tmp, "bad.png")
            with open(bad, "wb") as handle:
                handle.write(b"not-a-png")
            try:
                self._pixel_diff(good, bad, self._strict())
            except (ImportError, ModuleNotFoundError):
                self.fail("testing.goldens.diff is not implemented")
            except Exception:
                return
            self.fail("pixel_diff did not raise on corrupt PNG")

    def test_pixel_diff_uses_stdlib_only_no_pillow(self):
        # Plan Detail (quoted verbatim): "pure-stdlib pixel comparator
        # (PNG decode via stdlib `zlib`+`struct`, no Pillow)".
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            rows = _solid_rows(2, 2, (7, 8, 9, 255))
            a_path = _write_rgba_png(os.path.join(tmp, "a.png"), 2, 2, rows)
            b_path = _write_rgba_png(os.path.join(tmp, "b.png"), 2, 2, rows)
            self._pixel_diff(a_path, b_path, self._strict())
        self.assertNotIn("PIL", sys.modules)
        self.assertNotIn("Pillow", sys.modules)


class TestGradlewGoldenWiring(unittest.TestCase):
    TASKS = ("goldenCoverage", "goldenDiffSensitivity")

    def _gradlew_text(self):
        with open(GRADLEW_PATH, "r", encoding="utf-8") as handle:
            return handle.read()

    def test_gradlew_declares_golden_tasks(self):
        text = self._gradlew_text()
        for task in self.TASKS:
            self.assertIn(
                ":testing:%s" % task,
                text,
                "gradlew missing :testing:%s case" % task,
            )

    def test_gradlew_golden_tasks_exit_zero(self):
        import subprocess

        for task in self.TASKS:
            proc = subprocess.run(
                ["sh", GRADLEW_PATH, ":testing:%s" % task],
                cwd=REPO_ROOT,
                capture_output=True,
                text=True,
                timeout=120,
            )
            self.assertEqual(
                proc.returncode,
                0,
                ":testing:%s exited %s: %s"
                % (task, proc.returncode, proc.stderr[-2000:]),
            )

    def test_gradlew_unknown_testing_task_fails_nonzero(self):
        import subprocess

        proc = subprocess.run(
            ["sh", GRADLEW_PATH, ":testing:noSuchTaskXYZ"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertNotEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
