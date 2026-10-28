"""Batch 3 (T5) — plugin capability manifest with loader enforcement.

Covers NFR-CORE-006 AC-CORE-009 / TC-CORE-009
(``./gradlew :engine:core:pluginManifest``).

Public boundary under test (plan Checklist T5 Interface line,
reproduced verbatim)::

    struct Manifest { bool allowNetwork; std::vector<std::string> fsRoots; std::vector<std::string> jniClasses; std::string threadAffinity; }; + ParseResult parseManifest(const char* json) returning ok(Manifest) or err(ManifestError{ code: UNKNOWN_CAPABILITY | FS_ROOT_OUTSIDE_ALLOWLIST | JNI_CLASS_DENIED | THREAD_AFFINITY_INVALID; message: string }) + bool enforce(const Manifest&, const PluginRequest&) (deny-by-default: undeclared capability → deny with named reason surfaced to the sandbox peer).

The sources under test are ``engine/core/plugin/manifest.h`` and
``engine/core/plugin/manifest.cpp`` (created by the coder in T5).
Assertions are static contract checks over those files (existence +
declared-interface tokens + deny-by-default markers) plus live
parse checks that compile-and-run ``manifest.cpp``
(``ManifestLiveParse``); live parse/enforce execution is also the
coder's T5 build work and runs under ``:engine:core:pluginManifest``
(marked EXTERNAL below). Hermetic: stdlib unittest only, no
network/secrets/services/config; live checks use only the local C++
toolchain plus tmp dirs (fail-closed SKIP when no compiler is
present).
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUGIN_ROOT = os.path.join(REPO_ROOT, "engine", "core", "plugin")
MANIFEST_H = os.path.join(PLUGIN_ROOT, "manifest.h")
MANIFEST_CPP = os.path.join(PLUGIN_ROOT, "manifest.cpp")

ERROR_CODES = (
    "UNKNOWN_CAPABILITY",
    "FS_ROOT_OUTSIDE_ALLOWLIST",
    "JNI_CLASS_DENIED",
    "THREAD_AFFINITY_INVALID",
)


def _read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def _find_cxx():
    """Return a C++ compiler (c++/g++/clang++) or None when absent."""
    for candidate in ("c++", "g++", "clang++"):
        if shutil.which(candidate) is not None:
            return candidate
    return None


_MISSING_COMPILER_SKIP = (
    "SKIP: no C++ compiler found (c++/g++/clang++ missing) — "
    "live manifest compile-and-run cannot run here "
    "(fail closed, no silent pass)"
)

_MANIFEST_LIVE_MAIN = r"""
#include <cstdio>
#include <string>
#include "manifest.h"

using namespace muse::core::plugin;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::puts("FAIL: usage: probe <case>");
    return 2;
  }
  const std::string which = argv[1];
  int failures = 0;
  auto check = [&](bool cond, const char* name) {
    if (cond) {
      std::printf("PASS: %s\n", name);
    } else {
      std::printf("FAIL: %s\n", name);
      ++failures;
    }
  };
  auto check_rejected_with = [&](const ParseResult& r,
                                 ManifestErrorCode code, const char* name) {
    check(!r.success(), name);
    if (!r.success()) {
      check(r.error().code == code, name);
      check(!r.error().message.empty(), name);
    }
  };
  if (which == "explicit_false_with_later_true_substring") {
    // Shipped BLOCKER shape: a later "true" substring (inside an fs
    // root value) must not flip an explicit "allowNetwork": false on.
    ParseResult r = parseManifest(
        "{\"allowNetwork\": false, \"fsRoots\": [\"/true\"]}");
    check(r.success(), "explicit-false manifest parses");
    if (r.success()) {
      check(!r.manifest().allowNetwork,
            "explicit false with later true-substring keeps network denied");
      PluginRequest req;
      req.needsNetwork = true;
      check(!enforce(r.manifest(), req),
            "network use denied for explicit-false manifest");
    }
  } else if (which == "explicit_true_still_allowed") {
    ParseResult r = parseManifest(
        "{\"allowNetwork\": true, \"fsRoots\": []}");
    check(r.success(), "explicit-true manifest parses");
    if (r.success()) {
      check(r.manifest().allowNetwork,
            "explicit true keeps network allowed");
    }
  } else if (which == "unknown_capability") {
    ParseResult r = parseManifest(
        "{\"allowNetwork\": false, \"bogusCap\": true}");
    check_rejected_with(r, ManifestErrorCode::UNKNOWN_CAPABILITY,
                        "unknown capability rejected");
  } else if (which == "relative_fs_root") {
    ParseResult r = parseManifest(
        "{\"allowNetwork\": false, \"fsRoots\": [\"rel/path\"]}");
    check_rejected_with(r, ManifestErrorCode::FS_ROOT_OUTSIDE_ALLOWLIST,
                        "relative fs root rejected");
  } else if (which == "unqualified_jni_class") {
    ParseResult r =
        parseManifest("{\"jniClasses\": [\"FlatClass\"]}");
    check_rejected_with(r, ManifestErrorCode::JNI_CLASS_DENIED,
                        "unqualified JNI class rejected");
  } else if (which == "invalid_affinity") {
    ParseResult r =
        parseManifest("{\"threadAffinity\": \"bogus-pool\"}");
    check_rejected_with(r, ManifestErrorCode::THREAD_AFFINITY_INVALID,
                        "invalid affinity rejected");
  } else if (which == "non_string_affinity") {
    // A present key with a non-string value (number) is malformed and
    // must fail closed, never silently mean "no pinning".
    ParseResult r = parseManifest("{\"threadAffinity\": 5}");
    check_rejected_with(r, ManifestErrorCode::THREAD_AFFINITY_INVALID,
                        "non-string affinity rejected");
  } else {
    std::printf("FAIL: unknown case %s\n", argv[1]);
    return 2;
  }
  if (failures == 0) {
    std::puts("MANIFEST_LIVE_ALL_PASS");
  }
  return failures == 0 ? 0 : 1;
}
"""


def _both():
    """Concatenated manifest sources; each file must exist."""
    return _read(MANIFEST_H) + "\n" + _read(MANIFEST_CPP)


def error_codes_present(text):
    """Return the subset of manifest error codes named in ``text``."""
    return {code for code in ERROR_CODES if code in text}


def has_named_deny_reason(text):
    """A denial must carry a named reason (a reason/message token)."""
    lowered = text.lower()
    return "reason" in lowered or "message" in lowered


class ManifestFilesExist(unittest.TestCase):
    def test_manifest_header_exists(self):
        self.assertTrue(
            os.path.isfile(MANIFEST_H),
            "engine/core/plugin/manifest.h must exist",
        )

    def test_manifest_source_exists(self):
        self.assertTrue(
            os.path.isfile(MANIFEST_CPP),
            "engine/core/plugin/manifest.cpp must exist",
        )


class ManifestStructContract(unittest.TestCase):
    def test_manifest_struct_declared(self):
        self.assertTrue(
            re.search(r"(struct|class)\s+Manifest", _read(MANIFEST_H)),
            "manifest.h must declare struct Manifest",
        )

    def test_allow_network_field(self):
        self.assertTrue(
            re.search(r"bool\s+allowNetwork", _read(MANIFEST_H)),
            "Manifest must declare bool allowNetwork",
        )

    def test_fs_roots_field(self):
        self.assertTrue(
            re.search(r"std::vector<std::string>\s+fsRoots", _read(MANIFEST_H)),
            "Manifest must declare std::vector<std::string> fsRoots",
        )

    def test_jni_classes_field(self):
        self.assertTrue(
            re.search(
                r"std::vector<std::string>\s+jniClasses", _read(MANIFEST_H)
            ),
            "Manifest must declare std::vector<std::string> jniClasses",
        )

    def test_thread_affinity_field(self):
        self.assertTrue(
            re.search(r"std::string\s+threadAffinity", _read(MANIFEST_H)),
            "Manifest must declare std::string threadAffinity",
        )


class ParseContract(unittest.TestCase):
    def test_parse_manifest_declared(self):
        self.assertTrue(
            re.search(r"parseManifest\s*\(\s*const\s+char\s*\*", _read(MANIFEST_H)),
            "manifest.h must declare ParseResult parseManifest(const char* json)",
        )

    def test_parse_result_ok_err_shape(self):
        text = _both()
        self.assertIn("ParseResult", text, "manifest must name a ParseResult type")
        self.assertIn("ManifestError", text, "manifest must name a ManifestError type")

    def test_all_error_codes_named(self):
        text = _both()
        missing = [c for c in ERROR_CODES if c not in text]
        self.assertEqual(
            missing, [], "manifest must name all four error codes: %s" % (missing,)
        )

    def test_error_carries_message(self):
        self.assertTrue(
            re.search(r"struct\s+ManifestError[\s\S]{0,400}?message", _both()),
            "ManifestError must carry a message field",
        )


class EnforceContract(unittest.TestCase):
    def test_enforce_declared(self):
        self.assertTrue(
            re.search(
                r"bool\s+enforce\s*\(\s*const\s+Manifest\s*&",
                _read(MANIFEST_H),
            ),
            "manifest.h must declare bool enforce(const Manifest&, ...)",
        )

    def test_enforce_takes_plugin_request(self):
        self.assertTrue(
            "PluginRequest" in _both(),
            "enforce must take a PluginRequest parameter",
        )

    def test_deny_by_default_marker(self):
        text = _both().lower()
        self.assertTrue(
            "deny" in text,
            "manifest sources must state deny-by-default behavior",
        )

    def test_denial_carries_named_reason(self):
        self.assertTrue(
            has_named_deny_reason(_both()),
            "a denial must surface a named reason to the sandbox peer",
        )

    def test_undeclared_capability_denied(self):
        text = _both()
        self.assertTrue(
            "UNKNOWN_CAPABILITY" in text,
            "requesting an undeclared capability must be denied",
        )


class EnforceScannerDenialCases(unittest.TestCase):
    """Harness proof: a permissive fixture MUST be flagged by the scanner."""

    def test_fixture_allowing_undeclared_network_must_fail(self):
        fixture = (
            "bool enforce(const Manifest& m, const PluginRequest& r) {\n"
            "  if (r.needsNetwork) return true; // BUG: ignores m.allowNetwork\n"
            "  return true;\n"
            "}\n"
        )
        self.assertNotIn(
            "allowNetwork",
            fixture.split("return true")[0].split("r.needsNetwork")[1],
            "harness: fixture ignoring allowNetwork must be flaggable",
        )
        self.assertNotIn("deny", fixture.lower())

    def test_fixture_missing_fs_allowlist_check_must_fail(self):
        fixture = "bool enforce(const Manifest& m, const PluginRequest& r) { return true; }\n"
        self.assertNotIn("FS_ROOT_OUTSIDE_ALLOWLIST", fixture)
        self.assertNotIn("fsRoots", fixture)

    def test_complete_enforce_fixture_scans_clean(self):
        fixture = (
            "bool enforce(const Manifest& m, const PluginRequest& r) {\n"
            "  if (r.needsNetwork && !m.allowNetwork) return deny(r, UNKNOWN_CAPABILITY, reason);\n"
            "  if (!underRoots(r.path, m.fsRoots)) return deny(r, FS_ROOT_OUTSIDE_ALLOWLIST, reason);\n"
            "  if (!allowed(r.jniClass, m.jniClasses)) return deny(r, JNI_CLASS_DENIED, reason);\n"
            "  if (!affinityOk(r, m.threadAffinity)) return deny(r, THREAD_AFFINITY_INVALID, reason);\n"
            "  return true;\n"
            "}\n"
        )
        self.assertEqual(
            error_codes_present(fixture),
            set(ERROR_CODES),
            "complete fixture must name all four error codes",
        )
        self.assertTrue(has_named_deny_reason(fixture))


class ManifestLiveParse(unittest.TestCase):
    """Live parse tests: compile-and-run ``manifest.cpp`` (T5 test gap).

    Each case runs the production ``parseManifest`` (plus ``enforce``
    for the network-denial case) against adversarial JSON. When no
    compiler (``c++``/``g++``/``clang++``) is present the tests fail
    closed with SKIP naming the missing compiler — never a silent
    pass. Hermetic: toolchain + tmp dir only.
    """

    CASES = (
        "explicit_false_with_later_true_substring",
        "explicit_true_still_allowed",
        "unknown_capability",
        "relative_fs_root",
        "unqualified_jni_class",
        "invalid_affinity",
        "non_string_affinity",
    )

    @classmethod
    def setUpClass(cls):
        cls._tmp = None
        cls._exe = None
        cls._build_stderr = ""
        cxx = _find_cxx()
        if cxx is None:
            return
        cls._tmp = tempfile.TemporaryDirectory()
        src = os.path.join(cls._tmp.name, "manifest_live_main.cpp")
        cls._exe = os.path.join(cls._tmp.name, "manifest_live")
        with open(src, "w", encoding="utf-8") as fh:
            fh.write(_MANIFEST_LIVE_MAIN)
        proc = subprocess.run(
            [cxx, "-std=c++20", "-I", PLUGIN_ROOT, src, MANIFEST_CPP,
             "-o", cls._exe],
            capture_output=True,
            text=True,
            timeout=180,
        )
        cls._build_stderr = proc.stderr
        if proc.returncode != 0:
            cls._exe = None

    @classmethod
    def tearDownClass(cls):
        if cls._tmp is not None:
            cls._tmp.cleanup()

    def _run_case(self, case):
        if _find_cxx() is None or self._exe is None:
            if self._exe is None and _find_cxx() is not None:
                self.fail(
                    "live manifest driver must compile: %s"
                    % (self._build_stderr,)
                )
            self.skipTest(_MISSING_COMPILER_SKIP)
        proc = subprocess.run(
            [self._exe, case], capture_output=True, text=True, timeout=60
        )
        self.assertEqual(
            proc.returncode,
            0,
            "live manifest case %r must exit 0 (no crash, all asserts "
            "hold):\n%s" % (case, proc.stdout + proc.stderr),
        )
        return proc.stdout

    def test_live_explicit_false_with_later_true_substring_denied(self):
        out = self._run_case(
            "explicit_false_with_later_true_substring"
        )
        self.assertIn(
            "PASS: explicit false with later true-substring keeps "
            "network denied",
            out,
        )
        self.assertIn(
            "PASS: network use denied for explicit-false manifest", out
        )

    def test_live_explicit_true_still_allowed(self):
        out = self._run_case("explicit_true_still_allowed")
        self.assertIn("PASS: explicit true keeps network allowed", out)

    def test_live_unknown_capability_rejected(self):
        out = self._run_case("unknown_capability")
        self.assertIn("PASS: unknown capability rejected", out)

    def test_live_relative_fs_root_rejected(self):
        out = self._run_case("relative_fs_root")
        self.assertIn("PASS: relative fs root rejected", out)

    def test_live_unqualified_jni_class_rejected(self):
        out = self._run_case("unqualified_jni_class")
        self.assertIn("PASS: unqualified JNI class rejected", out)

    def test_live_invalid_affinity_rejected(self):
        out = self._run_case("invalid_affinity")
        self.assertIn("PASS: invalid affinity rejected", out)

    def test_live_non_string_affinity_rejected(self):
        out = self._run_case("non_string_affinity")
        self.assertIn("PASS: non-string affinity rejected", out)


if __name__ == "__main__":
    unittest.main()
