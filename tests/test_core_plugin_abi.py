"""Batch 2 (T3) — versioned plugin ABI handshake contract check.

Covers NFR-CORE-006 AC-CORE-007 + NFR-CORE-003 AC-CORE-004 /
TC-CORE-004 + TC-CORE-007 (``./gradlew :engine:core:pluginAbi``).

Public boundary under test (plan Checklist T3 Interface line,
reproduced verbatim)::

    PluginDescriptor { uint32_t abiMajor; uint32_t abiMinor; char abiHash[65]; const char* entryPoint; }; + LoadResult loadPlugin(const char* path, PluginDescriptor expected) returning ok(handle) or err(PluginError{ code: ABI_MAJOR_MISMATCH | ABI_MINOR_MISMATCH | ABI_HASH_MISMATCH; message: string; expectedHash, actualHash: string }). Single exported entry point symbol: extern "C" const PluginDescriptor* MusePluginEntry();.

The sources under test are ``engine/core/plugin/abi.h``,
``engine/core/plugin/loader.h`` and ``engine/core/plugin/loader.cpp``
(created by the coder in T3). Assertions are static contract checks over
those files (existence + declared-interface tokens + handshake/deny
markers) plus live load/reject checks that compile ``loader.cpp``,
build real ``.so`` plugins with crafted descriptors, and drive
``loadPlugin`` against them (``LoaderLiveLoad``); the Gradle task
wiring itself (``:engine:core:pluginAbi``) is the coder's T3 build work
and runs under that task (marked EXTERNAL below). Hermetic: stdlib
unittest only, no network/secrets/services/config; live checks use
only the local C++ toolchain plus tmp dirs (fail-closed SKIP when no
compiler is present).
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN_ROOT = os.path.join(REPO_ROOT, "engine", "core", "plugin")
ABI_H = os.path.join(PLUGIN_ROOT, "abi.h")
LOADER_H = os.path.join(PLUGIN_ROOT, "loader.h")
LOADER_CPP = os.path.join(PLUGIN_ROOT, "loader.cpp")

MISMATCH_CODES = (
    "ABI_MAJOR_MISMATCH",
    "ABI_MINOR_MISMATCH",
    "ABI_HASH_MISMATCH",
)


def _find_cxx():
    """Return a C++ compiler (c++/g++/clang++) or None when absent."""
    for candidate in ("c++", "g++", "clang++"):
        if shutil.which(candidate) is not None:
            return candidate
    return None


_MISSING_COMPILER_SKIP = (
    "SKIP: no C++ compiler found (c++/g++/clang++ missing) — "
    "live loader compile-and-run cannot run here "
    "(fail closed, no silent pass)"
)

_LOADER_LIVE_PLUGIN = r"""
#include <cstring>
#include "abi.h"

namespace muse {
namespace core {
namespace plugin {

namespace {

PluginDescriptor MakeCrafted() {
  PluginDescriptor d{};
  d.abiMajor = @@MAJOR@@u;
  d.abiMinor = @@MINOR@@u;
  // Deliberately no NUL terminator anywhere in the 65 hash bytes: the
  // loader diagnostic must use a length-limited copy, never an
  // unbounded %s / std::string(char*) over plugin-controlled memory.
  std::memset(d.abiHash, 'A', 65);
  d.entryPoint = "MusePluginEntry";
  return d;
}

}  // namespace

extern "C" const PluginDescriptor* MusePluginEntry() {
  static const PluginDescriptor kCrafted = MakeCrafted();
  return &kCrafted;
}

}  // namespace plugin
}  // namespace core
}  // namespace muse
"""

_LOADER_LIVE_MAIN = r"""
#include <cstdio>
#include <cstring>
#include <string>
#include "loader.h"

using namespace muse::core::plugin;

static size_t LongestRun(const std::string& s, char c) {
  size_t best = 0, cur = 0;
  for (char ch : s) {
    if (ch == c) {
      ++cur;
      if (cur > best) {
        best = cur;
      }
    } else {
      cur = 0;
    }
  }
  return best;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::puts("FAIL: usage: probe <plugin.so|__NULL__|__EMPTY__> <expected-code>");
    return 2;
  }
  PluginDescriptor expected{};
  expected.abiMajor = 1;
  expected.abiMinor = 0;
  std::memset(expected.abiHash, 'e', 64);
  expected.abiHash[64] = '\0';
  expected.entryPoint = "MusePluginEntry";
  const std::string pathArg = argv[1];
  const char* loadPath = argv[1];
  if (pathArg == "__NULL__") loadPath = nullptr;
  static const char kEmpty[] = "";
  if (pathArg == "__EMPTY__") loadPath = kEmpty;
  LoadResult result = loadPlugin(loadPath, expected);
  const std::string want = argv[2];
  int failures = 0;
  auto check = [&](bool cond, const char* name) {
    if (cond) {
      std::printf("PASS: %s\n", name);
    } else {
      std::printf("FAIL: %s\n", name);
      ++failures;
    }
  };
  check(!result.success(), "crafted plugin rejected");
  if (!result.success()) {
    const PluginError& err = result.error();
    std::string code = "UNKNOWN";
    if (err.code == PluginErrorCode::ABI_MAJOR_MISMATCH) {
      code = "ABI_MAJOR_MISMATCH";
    } else if (err.code == PluginErrorCode::ABI_MINOR_MISMATCH) {
      code = "ABI_MINOR_MISMATCH";
    } else if (err.code == PluginErrorCode::ABI_HASH_MISMATCH) {
      code = "ABI_HASH_MISMATCH";
    }
    check(code == want, "rejection carries expected code");
    // Null/empty-path arm: fail closed before any dlopen with the
    // published contract message naming the bad path. Reaching this
    // line at all proves no crash on the null/empty load path, and the
    // err(...) shape proves no self-handle ok().
    if (pathArg == "__NULL__" || pathArg == "__EMPTY__") {
      check(err.message.find("null or empty path") != std::string::npos,
            "null/empty path diagnosed by name");
      check(err.message.find("expected") != std::string::npos &&
                err.message.find("actual") != std::string::npos,
            "null/empty diagnostic names expected vs actual");
    }
    check(err.message.find("expected") != std::string::npos &&
              err.message.find("actual") != std::string::npos,
          "diagnostic names expected vs actual");
    check(err.expectedHash.size() <= 64 && err.actualHash.size() <= 64,
          "hashes bounded to 64 chars");
    check(err.message.size() < 1024, "diagnostic bounded");
    // The crafted hash is 65 non-NUL 'A' bytes: a length-limited copy
    // shows at most 64 consecutive 'A's, while an unbounded %s would
    // read past the array (65+ run) or crash the host. Reaching this
    // line at all proves no crash on the load path.
    check(LongestRun(err.message, 'A') <= 64,
          "no unbounded read past non-terminated hash");
  }
  if (failures == 0) {
    std::puts("LOADER_LIVE_ALL_PASS");
  }
  return failures == 0 ? 0 : 1;
}
"""


def _read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def handshake_codes_present(text):
    """Return the subset of mismatch codes named in ``text``."""
    return {code for code in MISMATCH_CODES if code in text}


def has_expected_actual_diagnostic(text):
    """A structured diagnostic must name expected vs actual hashes."""
    lowered = text.lower()
    return (
        "expectedhash" in lowered.replace("_", "").replace(" ", "")
        and "actualhash" in lowered.replace("_", "").replace(" ", "")
    ) or ("expected" in lowered and "actual" in lowered)


class PluginFilesExist(unittest.TestCase):
    def test_abi_header_exists(self):
        self.assertTrue(os.path.isfile(ABI_H), "engine/core/plugin/abi.h must exist")

    def test_loader_header_exists(self):
        self.assertTrue(
            os.path.isfile(LOADER_H), "engine/core/plugin/loader.h must exist"
        )

    def test_loader_source_exists(self):
        self.assertTrue(
            os.path.isfile(LOADER_CPP), "engine/core/plugin/loader.cpp must exist"
        )


class DescriptorContract(unittest.TestCase):
    def test_plugin_descriptor_struct(self):
        text = _read(ABI_H)
        self.assertTrue(
            re.search(r"(struct|class)\s+PluginDescriptor", text),
            "abi.h must declare PluginDescriptor",
        )

    def test_abi_major_field(self):
        text = _read(ABI_H)
        self.assertTrue(
            re.search(r"uint32_t\s+abiMajor", text),
            "PluginDescriptor must declare uint32_t abiMajor",
        )

    def test_abi_minor_field(self):
        text = _read(ABI_H)
        self.assertTrue(
            re.search(r"uint32_t\s+abiMinor", text),
            "PluginDescriptor must declare uint32_t abiMinor",
        )

    def test_abi_hash_field(self):
        text = _read(ABI_H)
        self.assertTrue(
            re.search(r"char\s+abiHash\s*\[\s*65\s*\]", text),
            "PluginDescriptor must declare char abiHash[65]",
        )

    def test_entry_point_field(self):
        text = _read(ABI_H)
        self.assertTrue(
            re.search(r"const\s+char\s*\*\s*entryPoint", text),
            "PluginDescriptor must declare const char* entryPoint",
        )


class EntryPointContract(unittest.TestCase):
    def test_single_exported_entry_symbol(self):
        found = False
        for path in (ABI_H, LOADER_H, LOADER_CPP):
            if os.path.isfile(path) and "MusePluginEntry" in _read(path):
                found = True
        self.assertTrue(
            found, "the single exported entry point MusePluginEntry must be declared"
        )

    def test_entry_point_extern_c_signature(self):
        combined = ""
        for path in (ABI_H, LOADER_H, LOADER_CPP):
            if os.path.isfile(path):
                combined += _read(path) + "\n"
        self.assertTrue(
            re.search(r'extern\s+"C"', combined),
            'entry point must use extern "C" linkage',
        )
        self.assertTrue(
            re.search(
                r"const\s+PluginDescriptor\s*\*\s*MusePluginEntry\s*\(\s*\)",
                combined,
            ),
            "entry point must be const PluginDescriptor* MusePluginEntry()",
        )


class LoaderContract(unittest.TestCase):
    def test_load_plugin_declared(self):
        text = _read(LOADER_H)
        self.assertTrue(
            re.search(r"loadPlugin\s*\(", text),
            "loader.h must declare loadPlugin",
        )
        self.assertTrue(
            re.search(
                r"loadPlugin\s*\(\s*const\s+char\s*\*.*PluginDescriptor",
                text,
                re.DOTALL,
            ),
            "loadPlugin must take (const char* path, PluginDescriptor expected)",
        )

    def test_load_result_ok_err_shape(self):
        text = _read(LOADER_H)
        self.assertIn("LoadResult", text)
        self.assertTrue(
            re.search(r"\bok\s*\(", text),
            "LoadResult must expose ok(handle)",
        )
        self.assertTrue(
            re.search(r"\berr\s*\(", text),
            "LoadResult must expose err(PluginError)",
        )

    def test_plugin_error_codes(self):
        combined = _read(ABI_H) + "\n" + _read(LOADER_H)
        for code in MISMATCH_CODES:
            with self.subTest(code=code):
                self.assertIn(
                    code,
                    combined,
                    "PluginError must define %s" % code,
                )

    def test_plugin_error_hash_fields(self):
        text = _read(LOADER_H)
        self.assertIn("PluginError", text)
        self.assertIn("message", text)
        self.assertTrue(
            re.search(r"expectedHash", text),
            "PluginError must carry expectedHash",
        )
        self.assertTrue(
            re.search(r"actualHash", text),
            "PluginError must carry actualHash",
        )


class HandshakeDenial(unittest.TestCase):
    def test_all_mismatch_codes_rejected_by_loader(self):
        text = _read(LOADER_CPP)
        present = handshake_codes_present(text)
        self.assertEqual(
            set(MISMATCH_CODES),
            present,
            "loader.cpp must reject all three mismatch codes, found %s" % sorted(present),
        )

    def test_structured_diagnostic_names_expected_vs_actual(self):
        text = _read(LOADER_CPP)
        self.assertTrue(
            has_expected_actual_diagnostic(text),
            "loader.cpp must emit a structured diagnostic naming expected vs actual",
        )

    def test_single_entry_resolution(self):
        text = _read(LOADER_CPP)
        self.assertIn(
            "MusePluginEntry",
            text,
            "loader.cpp must resolve only the MusePluginEntry symbol",
        )

    def test_reject_before_plugin_code_runs(self):
        text = _read(LOADER_CPP)
        self.assertTrue(
            re.search(r"\berr\s*\(", text),
            "loader.cpp must return err(...) on mismatch before running plugin code",
        )


class HandshakeScannerDenialCases(unittest.TestCase):
    """Harness-only checks: the handshake-code scanner used above must be
    able to fail on incomplete loader text (no production file needed)."""

    def test_complete_loader_text_passes(self):
        text = (
            "LoadResult loadPlugin(const char* path, PluginDescriptor expected) {\n"
            "  void* so = dlopen(path, RTLD_NOW);\n"
            "  auto* entry = (EntryFn) dlsym(so, \"MusePluginEntry\");\n"
            "  const PluginDescriptor* actual = entry();\n"
            "  if (actual->abiMajor != expected.abiMajor)\n"
            "    return err(PluginError{ABI_MAJOR_MISMATCH, msg, expectedHash, actualHash});\n"
            "  if (actual->abiMinor != expected.abiMinor)\n"
            "    return err(PluginError{ABI_MINOR_MISMATCH, msg, expectedHash, actualHash});\n"
            "  if (hash != expectedHash)\n"
            "    return err(PluginError{ABI_HASH_MISMATCH, msg, expectedHash, actualHash});\n"
            "  return ok(so);\n"
            "}\n"
        )
        self.assertEqual(set(MISMATCH_CODES), handshake_codes_present(text))
        self.assertTrue(has_expected_actual_diagnostic(text))

    def test_loader_missing_hash_check_must_fail(self):
        text = (
            "  if (actual->abiMajor != expected.abiMajor)\n"
            "    return err(PluginError{ABI_MAJOR_MISMATCH, msg});\n"
        )
        present = handshake_codes_present(text)
        self.assertNotEqual(
            set(MISMATCH_CODES),
            present,
            "a loader that never checks the hash must be flagged",
        )
        self.assertNotIn("ABI_HASH_MISMATCH", present)

    def test_loader_missing_minor_check_must_fail(self):
        text = (
            "  if (actual->abiMajor != expected.abiMajor)\n"
            "    return err(PluginError{ABI_MAJOR_MISMATCH, msg});\n"
            "  if (hash != expectedHash)\n"
            "    return err(PluginError{ABI_HASH_MISMATCH, msg});\n"
        )
        self.assertNotIn("ABI_MINOR_MISMATCH", handshake_codes_present(text))

    def test_diagnostic_without_actual_hash_must_fail(self):
        self.assertFalse(
            has_expected_actual_diagnostic("return err(PluginError{message});"),
            "a diagnostic naming neither expected nor actual must be flagged",
        )


class LoaderLiveLoad(unittest.TestCase):
    """Live load tests: drive ``loadPlugin`` against crafted ``.so``
    plugins (T3 test gap).

    Each plugin exports ``MusePluginEntry`` with a descriptor whose 65
    ``abiHash`` bytes contain no NUL terminator plus wrong
    major/minor/hash, so the handshake must return ``err(...)`` with a
    bounded expected-vs-actual diagnostic instead of crashing on the
    load path. When no compiler (``c++``/``g++``/``clang++``) is
    present the tests fail closed with SKIP naming the missing
    compiler — never a silent pass. Hermetic: toolchain + tmp dir
    only.
    """

    @classmethod
    def setUpClass(cls):
        cls._tmp = None
        cls._exe = None
        cls._so = {}
        cls._build_stderr = ""
        cxx = _find_cxx()
        if cxx is None:
            return
        cls._tmp = tempfile.TemporaryDirectory()
        tmp = cls._tmp.name
        variants = {
            "hash": (1, 0, "ABI_HASH_MISMATCH"),
            "major": (999, 888, "ABI_MAJOR_MISMATCH"),
        }
        for name, (major, minor, _want) in variants.items():
            src = os.path.join(tmp, "crafted_%s.cpp" % name)
            with open(src, "w", encoding="utf-8") as fh:
                fh.write(
                    _LOADER_LIVE_PLUGIN.replace("@@MAJOR@@", str(major))
                    .replace("@@MINOR@@", str(minor))
                )
            so = os.path.join(tmp, "crafted_%s.so" % name)
            proc = subprocess.run(
                [cxx, "-std=c++20", "-shared", "-fPIC",
                 "-I", PLUGIN_ROOT, src, "-o", so],
                capture_output=True,
                text=True,
                timeout=180,
            )
            if proc.returncode != 0:
                cls._build_stderr += proc.stderr
                return
            cls._so[name] = so
        main_src = os.path.join(tmp, "loader_live_main.cpp")
        with open(main_src, "w", encoding="utf-8") as fh:
            fh.write(_LOADER_LIVE_MAIN)
        cls._exe = os.path.join(tmp, "loader_live")
        proc = subprocess.run(
            [cxx, "-std=c++20", "-I", PLUGIN_ROOT, main_src,
             LOADER_CPP, "-o", cls._exe, "-ldl"],
            capture_output=True,
            text=True,
            timeout=180,
        )
        cls._build_stderr += proc.stderr
        if proc.returncode != 0:
            cls._exe = None

    @classmethod
    def tearDownClass(cls):
        if cls._tmp is not None:
            cls._tmp.cleanup()

    def _run_load(self, variant, want_code):
        if _find_cxx() is None or self._exe is None:
            if self._exe is None and _find_cxx() is not None:
                self.fail(
                    "live loader driver must compile: %s"
                    % (self._build_stderr,)
                )
            self.skipTest(_MISSING_COMPILER_SKIP)
        self.assertIn(
            variant,
            self._so,
            "crafted plugin %r must build" % (variant,),
        )
        proc = subprocess.run(
            [self._exe, self._so[variant], want_code],
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(
            proc.returncode,
            0,
            "live load case %r must exit 0 (rejected with err(...), "
            "no crash):\n%s" % (variant, proc.stdout + proc.stderr),
        )
        return proc.stdout

    def test_live_hash_mismatch_with_nonterminated_hash_rejected(self):
        out = self._run_load("hash", "ABI_HASH_MISMATCH")
        self.assertIn("PASS: crafted plugin rejected", out)
        self.assertIn("PASS: rejection carries expected code", out)
        self.assertIn(
            "PASS: diagnostic names expected vs actual", out
        )
        self.assertIn("PASS: hashes bounded to 64 chars", out)
        self.assertIn(
            "PASS: no unbounded read past non-terminated hash", out
        )
        self.assertIn("LOADER_LIVE_ALL_PASS", out)

    def test_live_major_mismatch_with_nonterminated_hash_rejected(self):
        out = self._run_load("major", "ABI_MAJOR_MISMATCH")
        self.assertIn("PASS: crafted plugin rejected", out)
        self.assertIn("PASS: rejection carries expected code", out)
        self.assertIn(
            "PASS: no unbounded read past non-terminated hash", out
        )
        self.assertIn("LOADER_LIVE_ALL_PASS", out)

    def _run_null_path(self, path_arg):
        if _find_cxx() is None or self._exe is None:
            if self._exe is None and _find_cxx() is not None:
                self.fail(
                    "live loader driver must compile: %s"
                    % (self._build_stderr,)
                )
            self.skipTest(_MISSING_COMPILER_SKIP)
        proc = subprocess.run(
            [self._exe, path_arg, "ABI_HASH_MISMATCH"],
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(
            proc.returncode,
            0,
            "live load case %r must exit 0 (err(ABI_HASH_MISMATCH), "
            "no crash, no self-handle ok()):\n%s"
            % (path_arg, proc.stdout + proc.stderr),
        )
        return proc.stdout

    def test_live_null_path_fail_closed(self):
        out = self._run_null_path("__NULL__")
        self.assertIn("PASS: crafted plugin rejected", out)
        self.assertIn("PASS: rejection carries expected code", out)
        self.assertIn("PASS: null/empty path diagnosed by name", out)
        self.assertIn(
            "PASS: null/empty diagnostic names expected vs actual", out
        )
        self.assertIn("LOADER_LIVE_ALL_PASS", out)

    def test_live_empty_path_fail_closed(self):
        out = self._run_null_path("__EMPTY__")
        self.assertIn("PASS: crafted plugin rejected", out)
        self.assertIn("PASS: rejection carries expected code", out)
        self.assertIn("PASS: null/empty path diagnosed by name", out)
        self.assertIn(
            "PASS: null/empty diagnostic names expected vs actual", out
        )
        self.assertIn("LOADER_LIVE_ALL_PASS", out)


if __name__ == "__main__":
    unittest.main()
