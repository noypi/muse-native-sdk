#!/usr/bin/env python3
"""Batch 4 (T8) no-new-shim-literal gate tests for `vendor-d3`.

Covers plan.md Batch 4 T8 (plan Interface line quoted verbatim in
unit-tests/T8.md, never copied here):
`TestGradlewFuzzWiring` remains the single allowed namer; every other
new literal fails.

CAUTION (plan T8): this file must not introduce the forbidden literal
itself. The literal under test is therefore never written out here —
it is assembled at runtime from two halves (`"gradle" + "w"`) and the
allowlist filename is joined from parts, so a byte scan of this file
finds no occurrence. Fixture content uses the same split; test and
symbol names never spell it.

Hermetic: Python stdlib `unittest` + `os`/`sys`/`tempfile` only; tmp
dirs for scanner fixtures; read-only scans of the run checkout; no
network/downloads/device/secrets; no subprocess (no shell-out scan).
"""

import os
import sys
import tempfile
import unittest

# The forbidden shim literal, assembled so these source bytes never
# contain it contiguously (T8 CAUTION).
_LIT_HEAD = "gradle"
_LIT_TAIL = "w"
LITERAL = _LIT_HEAD + _LIT_TAIL

_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
_RUN_CHECKOUT = os.path.dirname(
    os.path.dirname(os.path.dirname(_THIS_DIR))
)

_ALLOWLIST_RELPATH = os.path.join(
    "testing", "gate", _LIT_HEAD + _LIT_TAIL + "-allowlist.txt"
)
_ALLOWLIST_PATH = os.path.join(_RUN_CHECKOUT, _ALLOWLIST_RELPATH)

# The single allowed namer (TC-TESTING-038). Case-sensitive: this symbol
# never contains the lowercase literal, so naming it here is clean.
_FUZZ_WIRING = "TestGradlewFuzzWiring"

# Exact T1-T7 touched-file set (plan Files lines, batch test files
# included). Every entry must exist and carry no occurrence.
TOUCHED = (
    "tools/deps/third_party_pins.json",
    "toolchain/cli/thirdPartyEnv.js",
    "toolchain/cli/thirdPartyEnvD3.test.js",
    "engine/text/CMakeLists.txt",
    "engine/runtime/cmake/android-ndk.cmake",
    "engine/runtime/CMakeLists.txt",
    "build/dependency-manifest.json",
    "toolchain/deps/pins.json",
    "app/src/main/assets/third-party-notices.txt",
    "dist/license-report.json",
    "tools/deps/_deps_data.py",
    "tools/deps/tests/test_d3_wiring.py",
    "tools/deps/tests/test_d3_records_gates.py",
    "tools/deps/tests/test_d3_proof.py",
    "tools/deps/tests/test_d3_t8_gate.py",
    "engine/text/tests/harfbuzz_parity_test.cpp",
    "engine/runtime/tests/android_build_gate_test.cpp",
    "engine/runtime/tests/jni_contract_test.sh",
)

_EXCLUDED_DIRS = frozenset({"__pycache__"})


def _read_bytes(path):
    with open(path, "rb") as handle:
        return handle.read().decode("utf-8", errors="ignore")


def _scan_file(path):
    """True iff the file bytes contain the literal (decode-ignored)."""
    try:
        return LITERAL in _read_bytes(path)
    except OSError:
        return False


def find_offenders_in_tree(root, allow=()):
    """Hermetic tmp-tree scanner: repo-rel paths holding the literal."""
    allowed = set(allow)
    offenders = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in _EXCLUDED_DIRS]
        for filename in filenames:
            if filename.endswith((".pyc", ".pyo")):
                continue
            full = os.path.join(dirpath, filename)
            relpath = os.path.relpath(full, root)
            if relpath in allowed:
                continue
            if _scan_file(full):
                offenders.append(relpath)
    return sorted(offenders)


class TestSelfClean(unittest.TestCase):
    """This batch's own new files carry no occurrence (T8 CAUTION)."""

    def test_this_file_has_no_occurrence(self):
        self.assertFalse(
            _scan_file(os.path.abspath(__file__)),
            "this gate file introduces the forbidden literal",
        )

    def test_t7_proof_file_has_no_occurrence(self):
        proof = os.path.join(_THIS_DIR, "test_d3_proof.py")
        self.assertTrue(os.path.isfile(proof), "T7 file missing")
        self.assertFalse(
            _scan_file(proof),
            "T7 proof file introduces the forbidden literal",
        )


class TestTouchedFilesClean(unittest.TestCase):
    """No T1-T7 touched file holds the literal (the T8 gate proper)."""

    def test_every_touched_path_exists(self):
        for relpath in TOUCHED:
            self.assertTrue(
                os.path.exists(os.path.join(_RUN_CHECKOUT, relpath)),
                "touched file missing, gate list is stale: %s" % relpath,
            )

    def test_no_touched_file_holds_the_literal(self):
        offenders = [
            relpath for relpath in TOUCHED
            if _scan_file(os.path.join(_RUN_CHECKOUT, relpath))
        ]
        self.assertEqual(
            offenders, [],
            "new literal outside the grandfathered allowlist "
            "(AC-TESTING-038): %s" % offenders,
        )


class TestAllowlistContract(unittest.TestCase):
    """The allowlist still names exactly the grandfathered set."""

    def _load(self, case):
        try:
            return _read_bytes(_ALLOWLIST_PATH)
        except OSError:
            case.fail(
                "allowlist absent at %s (FR-TESTING-006 gate file)"
                % _ALLOWLIST_RELPATH
            )

    def test_allowlist_names_the_single_allowed_namer(self):
        text = self._load(self)
        hits = [ln for ln in text.splitlines() if _FUZZ_WIRING in ln]
        self.assertEqual(
            len(hits), 1,
            "the fuzz-wiring namer must remain the single allowed "
            "namer (AC-TESTING-038/039)",
        )

    def test_no_touched_file_was_allowlisted(self):
        text = self._load(self)
        entries = {
            ln.strip() for ln in text.splitlines()
            if ln.strip() and not ln.strip().startswith("#")
        }
        smuggled = [
            relpath for relpath in TOUCHED
            if relpath in entries
            or any(e == relpath or e.startswith(relpath + "::")
                   for e in entries)
        ]
        self.assertEqual(
            smuggled, [],
            "batch file allowlisted to excuse a literal "
            "(AC-TESTING-039): %s" % smuggled,
        )


class TestScannerPure(unittest.TestCase):
    """Scanner behavior on hermetic tmp trees (red-capable half)."""

    def _write(self, root, files):
        for relpath, content in files.items():
            full = os.path.join(root, *relpath.split("/"))
            parent = os.path.dirname(full)
            if parent:
                os.makedirs(parent, exist_ok=True)
            mode = "wb" if isinstance(content, bytes) else "w"
            kwargs = {} if isinstance(content, bytes) else {
                "encoding": "utf-8"}
            with open(full, mode, **kwargs) as handle:
                handle.write(content)

    def test_clean_tree_reports_no_offenders(self):
        with tempfile.TemporaryDirectory() as root:
            self._write(root, {"src/clean.py": "x = 1\n"})
            self.assertEqual(find_offenders_in_tree(root), [])

    def test_probe_literal_is_flagged_by_name(self):
        with tempfile.TemporaryDirectory() as root:
            probe = os.path.join("scripts", "new_probe.sh")
            self._write(
                root,
                {probe.replace(os.sep, "/"):
                 "#!/bin/sh\n./" + LITERAL + " :app:probe\n"},
            )
            self.assertEqual(find_offenders_in_tree(root), [probe])

    def test_allowlisted_path_is_not_flagged(self):
        with tempfile.TemporaryDirectory() as root:
            self._write(
                root, {"shim": "./" + LITERAL + " --help\n"}
            )
            self.assertEqual(
                find_offenders_in_tree(root, allow={"shim"}), []
            )
            self.assertEqual(
                find_offenders_in_tree(root), ["shim"]
            )

    def test_bytecode_is_never_scan_scope(self):
        with tempfile.TemporaryDirectory() as root:
            self._write(
                root,
                {"pkg/__pycache__/m.pyc": b"\x00" + LITERAL.encode()},
            )
            self.assertEqual(find_offenders_in_tree(root), [])

    def test_results_are_sorted(self):
        with tempfile.TemporaryDirectory() as root:
            self._write(
                root,
                {"z.py": LITERAL + "\n", "a.py": LITERAL + "\n"},
            )
            self.assertEqual(
                find_offenders_in_tree(root), ["a.py", "z.py"]
            )


if __name__ == "__main__":
    unittest.main()
