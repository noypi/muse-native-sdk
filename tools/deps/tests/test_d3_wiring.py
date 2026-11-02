#!/usr/bin/env python3
"""Batch 2 (T3, T4) hermetic structural wiring tests for `vendor-d3`.

Covers plan.md Batch 2 "Static sub-builds + Runtime link":
- T3 (plan Interface: `cmakeConfigure(useSystemLibs: OFF,
  thirdPartyDir: $MUSE_NATIVE_THIRD_PARTY_DIR) -> muse_text(+D3 static
  objects)`): in-tree static sub-builds of HarfBuzz + FreeType + ICU for
  the host lane and the Android NDK lane; no system find_package(HarfBuzz)
  path; missing $MUSE_NATIVE_THIRD_PARTY_DIR fails naming the var.
- T4 (plan Interface: `link(Runtime SHARED <- muse_text(+harfbuzz,
  +freetype, +icudata))`): the Android `Runtime` SHARED target gains the
  D3 static objects; host `runtime` static lane unchanged; weak-ref probes
  (hb_shape, FT_Init_FreeType, u_getVersion) present.

Hermetic: Python stdlib `unittest` + `os` only. Read-only file
assertions over the wiring files — no configure, no compile, no network,
no secrets, no application config, no live service, no subprocess. These
tests FAIL RED until the coder writes the wiring; the multi-minute
ICU/HarfBuzz/FreeType build itself is never a test step.

Per GR-08 T1.8 this file is assigned to the first item (T3) and mapped
in both unit-tests/T3.md and unit-tests/T4.md.
"""

import os
import unittest

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_RUN_CHECKOUT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)

_TEXT_CMAKE = os.path.join(_RUN_CHECKOUT, "engine", "text",
                           "CMakeLists.txt")
_RUNTIME_CMAKE = os.path.join(_RUN_CHECKOUT, "engine", "runtime",
                               "CMakeLists.txt")
_NDK_CMAKE = os.path.join(_RUN_CHECKOUT, "engine", "runtime", "cmake",
                          "android-ndk.cmake")
_SHAPER_CPP = os.path.join(_RUN_CHECKOUT, "engine", "text", "src",
                           "shaper.cpp")

_VAR = "MUSE_NATIVE_THIRD_PARTY_DIR"


def _read(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def _lines(body):
    return body.splitlines()


def _runtime_shared_region(body):
    """Text of the Android `Runtime` SHARED block to end of file."""
    marker = "add_library(Runtime SHARED"
    index = body.find(marker)
    if index < 0:
        return ""
    return body[index:]


class TestT3StaticSubBuilds(unittest.TestCase):
    """T3: in-tree static D3 sub-builds, host + Android NDK lanes."""

    def test_text_cmake_references_third_party_dir(self):
        body = _read(_TEXT_CMAKE)
        self.assertIn(_VAR, body,
                      "engine/text/CMakeLists.txt must resolve sources "
                      "via $%s" % _VAR)

    def test_text_cmake_missing_var_fails_naming_var(self):
        body = _read(_TEXT_CMAKE)
        self.assertIn("FATAL_ERROR", body,
                      "missing $%s must fail configure, not warn" % _VAR)
        anchor = body.find("FATAL_ERROR")
        window = body[max(0, anchor - 2000):anchor + 2000]
        self.assertIn(_VAR, window,
                      "fail message must name %s" % _VAR)
        self.assertIn("export %s=" % _VAR, window,
                      "fail message must show an example export")

    def test_no_system_find_package_harfbuzz_path(self):
        for path in (_TEXT_CMAKE, _RUNTIME_CMAKE, _NDK_CMAKE):
            body = _read(path).lower()
            self.assertNotIn("find_package(harfbuzz", body,
                             "system HarfBuzz path forbidden in %s" % path)

    def test_harfbuzz_no_icu_knob_explicit(self):
        body = _read(_TEXT_CMAKE)
        self.assertIn("HB_HAVE_ICU", body,
                      "HarfBuzz ICU knob must be explicit (SD6 verified "
                      "default is OFF)")
        self.assertIn("OFF", body)

    def test_harfbuzz_utils_off(self):
        body = _read(_TEXT_CMAKE)
        self.assertIn("HB_BUILD_UTILS", body)
        hit = [line for line in _lines(body)
               if "HB_BUILD_UTILS" in line
               and ("OFF" in line or "FALSE" in line)]
        self.assertTrue(hit,
                        "HB_BUILD_UTILS must be explicitly OFF "
                        "(same-line shape per contract ASSUMPTION)")

    def test_harfbuzz_optional_modules_off_unless_required(self):
        # ASSUMPTION: each optional module (subset/raster/vector/gpu) is
        # disabled with an explicit OFF/FALSE knob on the same line unless
        # a T-item requires it; the exact upstream knob names live with
        # the coder, the disabled-by-default shape is the contract.
        body = _read(_TEXT_CMAKE)
        lowered = body.lower()
        for module in ("subset", "raster", "vector", "gpu"):
            hit = [line for line in lowered.splitlines()
                   if module in line
                   and ("off" in line or "false" in line)]
            self.assertTrue(hit,
                            "HarfBuzz optional module '%s' must be "
                            "explicitly OFF unless a T-item requires it"
                            % module)

    def test_icu_host_first_cross_build_static_archive(self):
        body = _read(_TEXT_CMAKE)
        for token in ("--with-cross-build",
                      "--enable-static",
                      "--disable-shared",
                      "--with-data-packaging=archive",
                      "-DU_CHARSET_IS_UTF8=1"):
            self.assertIn(token, body,
                          "ICU wiring missing token: %s" % token)
        self.assertTrue("runConfigureICU" in body
                        or "host" in body.lower(),
                        "ICU wiring must name the host-tools-first build")

    def test_icu_without_host_build_fails_closed(self):
        # ASSUMPTION: exact message shape is the coder's, but it must be
        # FATAL_ERROR naming the host/cross-build dir.
        body = _read(_TEXT_CMAKE)
        self.assertIn("FATAL_ERROR", body)
        lowered = body.lower()
        self.assertTrue("cross-build" in lowered
                        or "cross_build" in lowered
                        or "host" in lowered,
                        "ICU wiring must fail closed when the host build "
                        "dir is absent")

    def test_freetype_toggles_from_vendored_tree(self):
        # SD8: toggles read from the vendored tree (ftoption.h +
        # ftmodule.h/modules.cfg + ftsystem.c) — no invented flags.
        body = _read(_TEXT_CMAKE).lower()
        self.assertIn("ftoption", body,
                      "FreeType wiring must reference vendored ftoption.h")
        self.assertTrue("ftmodule" in body or "modules.cfg" in body,
                        "FreeType wiring must reference vendored "
                        "ftmodule.h/modules.cfg")
        self.assertTrue("ftsystem" in body,
                        "FreeType wiring must reference vendored ftsystem")

    def test_android_ndk_lane_configures_d3(self):
        body = _read(_NDK_CMAKE)
        lowered = body.lower()
        self.assertTrue("d3" in lowered
                        or "third-party" in lowered
                        or "third_party" in lowered,
                        "android-ndk.cmake must configure the D3 lane, "
                        "not just host paths")


class TestT4RuntimeStaticLink(unittest.TestCase):
    """T4: D3 static objects linked into the Android `Runtime` target."""

    def test_runtime_shared_target_gains_d3_static_objects(self):
        # "Static objects" shape (ASSUMPTION): the Runtime region must
        # carry a static-link token — a TARGET_OBJECTS reference, a `.a`
        # archive, or a `_static`/OBJECT target name — alongside the
        # three D3 dep names. The existing find_library shared-mirror
        # (MUSE_TEXT_D3_LIBS, .so names) satisfies neither half.
        region = _runtime_shared_region(_read(_RUNTIME_CMAKE))
        lowered = region.lower()
        self.assertTrue(region,
                        "no add_library(Runtime SHARED block found")
        self.assertTrue("target_objects" in lowered
                        or ".a" in lowered
                        or "_static" in lowered,
                        "Runtime SHARED block must link D3 as STATIC "
                        "objects, not shared libraries")
        for dep in ("harfbuzz", "freetype", "icu"):
            self.assertIn(dep, lowered,
                          "Runtime SHARED block must gain D3 static "
                          "object '%s' per link(Runtime SHARED <- "
                          "muse_text(+harfbuzz, +freetype, +icudata))"
                          % dep)

    def test_runtime_block_guarded_by_android_abi(self):
        body = _read(_RUNTIME_CMAKE)
        guard = body.find("if(DEFINED ANDROID_ABI")
        target = body.find("add_library(Runtime SHARED")
        self.assertGreaterEqual(guard, 0,
                                "DEFINED ANDROID_ABI guard missing")
        self.assertGreater(target, guard,
                           "Runtime SHARED block must sit under the "
                           "DEFINED ANDROID_ABI guard")

    def test_weak_probes_present(self):
        pooled = _read(_SHAPER_CPP) + _read(_RUNTIME_CMAKE)
        for symbol in ("hb_shape", "FT_Init_FreeType", "u_getVersion"):
            self.assertIn(symbol, pooled,
                          "weak-ref probe '%s' must resolve non-null "
                          "on device" % symbol)

    def test_host_runtime_static_lane_unchanged(self):
        body = _read(_RUNTIME_CMAKE)
        for line in _lines(body):
            stripped = line.strip()
            if stripped.startswith("add_library(runtime"):
                self.assertNotIn("SHARED", stripped,
                                 "host `runtime` lane must stay STATIC")
                return
        self.fail("host add_library(runtime ...) target missing")


class TestNoNetworkByDesign(unittest.TestCase):
    """Hermeticity: this batch test performs file reads only."""

    _ALLOWED_IMPORTS = frozenset({"os", "unittest"})

    def test_only_hermetic_stdlib_imports(self):
        with open(os.path.abspath(__file__), "r",
                   encoding="utf-8") as handle:
            lines = handle.readlines()
        imported = set()
        for line in lines:
            stripped = line.strip()
            if stripped.startswith("import "):
                imported.add(stripped.split()[1].split(".")[0])
            elif stripped.startswith("from ") and " import " in stripped:
                imported.add(stripped.split()[1].split(".")[0])
        self.assertTrue(imported)
        self.assertLessEqual(imported, self._ALLOWED_IMPORTS,
                              "non-hermetic import in batch test: %s"
                              % sorted(imported - self._ALLOWED_IMPORTS))

    def test_no_network_or_process_tokens_in_test_bodies(self):
        forbidden = ("socket", "urllib", "http", "curl", "wget",
                     "subprocess", "os.system", "os.popen",
                     "child_process", "execsync", "network")
        with open(os.path.abspath(__file__), "r",
                   encoding="utf-8") as handle:
            source = handle.read()
        start = source.index("class TestT3StaticSubBuilds")
        end = source.index("class TestNoNetworkByDesign")
        lowered = source[start:end].lower()
        for token in forbidden:
            self.assertNotIn(token, lowered,
                              "network-capable token in test body: %s"
                              % token)


if __name__ == "__main__":
    unittest.main()
