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
markers); live ``dlopen`` load/reject execution is the coder's T3 build
work and runs under ``:engine:core:pluginAbi`` (marked EXTERNAL below —
the repo's ``gradlew`` shim currently exposes only the ``:benchmark:*``
and ``:engine:core:allocator`` surface). Hermetic: stdlib unittest only,
no network/secrets/services/config, no compilation.
"""

import os
import re
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


if __name__ == "__main__":
    unittest.main()
