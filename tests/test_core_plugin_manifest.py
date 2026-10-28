"""Batch 3 (T5) — plugin capability manifest with loader enforcement.

Covers NFR-CORE-006 AC-CORE-009 / TC-CORE-009
(``./gradlew :engine:core:pluginManifest``).

Public boundary under test (plan Checklist T5 Interface line,
reproduced verbatim)::

    struct Manifest { bool allowNetwork; std::vector<std::string> fsRoots; std::vector<std::string> jniClasses; std::string threadAffinity; }; + ParseResult parseManifest(const char* json) returning ok(Manifest) or err(ManifestError{ code: UNKNOWN_CAPABILITY | FS_ROOT_OUTSIDE_ALLOWLIST | JNI_CLASS_DENIED | THREAD_AFFINITY_INVALID; message: string }) + bool enforce(const Manifest&, const PluginRequest&) (deny-by-default: undeclared capability → deny with named reason surfaced to the sandbox peer).

The sources under test are ``engine/core/plugin/manifest.h`` and
``engine/core/plugin/manifest.cpp`` (created by the coder in T5).
Assertions are static contract checks over those files (existence +
declared-interface tokens + deny-by-default markers); live
parse/enforce execution is the coder's T5 build work and runs under
``:engine:core:pluginManifest`` (marked EXTERNAL below). Hermetic:
stdlib unittest only, no network/secrets/services/config, no
compilation.
"""

import os
import re
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


if __name__ == "__main__":
    unittest.main()
