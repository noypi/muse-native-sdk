"""Batch 1 (T2) — per-frame arena + pre-sized pool allocator contract check.

Covers NFR-CORE-002 AC-CORE-003 / TC-CORE-003
(``./gradlew :engine:core:allocator``).

Public boundary under test (plan Checklist T2 Interface line,
reproduced verbatim)::

    Arena(size_t capacity) :: void* allocate(size_t n, size_t align) noexcept; void reset() noexcept; + Pool<T,N> :: T* acquire() noexcept; void release(T*) noexcept; + RenderAlloc :: void* operator new(size_t) = delete on the render path (global new/malloc calls inside a frame fail the instrumented build). Error shape: allocation outside arena/pool returns nullptr and logs CORE_ALLOC_HEAP_ESCAPE in instrumented builds.

The sources under test are ``engine/core/mem/arena.h``,
``engine/core/mem/arena.cpp``, ``engine/core/mem/pool.h`` and
``engine/core/mem/render_alloc.h`` (created by the coder in T2).
Assertions are static contract checks over those files (existence +
declared-interface tokens + error-shape marker) plus live behavioral
checks that compile-and-run ``arena.cpp`` (``ArenaLiveBehavior``); the
Gradle task wiring itself (``:engine:core:allocator``) is the coder's
T2 build work and is marked EXTERNAL below — the repo's ``gradlew``
shim currently exposes only the ``:benchmark:*`` surface. Hermetic:
stdlib unittest only, no network/secrets/services/config; live checks
use only the local C++ toolchain plus tmp dirs (fail-closed SKIP when
no compiler is present).

Expected state before T2 implementation: RED — ``engine/core/mem`` does
not exist, so every file-existence/interface assertion fails. The
injected-fixture escape-scanner tests pass from the start (they only
need the in-file scanner + tmp dirs).
"""

import os
import re
import shutil
import subprocess
import tempfile
import unittest

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MEM_ROOT = os.path.join(REPO_ROOT, "engine", "core", "mem")
ARENA_H = os.path.join(MEM_ROOT, "arena.h")
ARENA_CPP = os.path.join(MEM_ROOT, "arena.cpp")
POOL_H = os.path.join(MEM_ROOT, "pool.h")
RENDER_ALLOC_H = os.path.join(MEM_ROOT, "render_alloc.h")

# Global-heap escape pattern: a render-path TU calling the global heap
# directly instead of the arena/pool. The scanner flags such lines unless
# the line is part of the ban declaration itself (deleted new / = delete).
HEAP_ESCAPE_RE = re.compile(
    r"(?<!:)(?<![\w>])(::)?\b(new\b|malloc\s*\(|calloc\s*\(|realloc\s*\()"
)
BAN_DECL_RE = re.compile(r"=\s*delete|CORE_ALLOC_HEAP_ESCAPE|operator\s+new")


def scan_heap_escapes(path):
    """Return 1-based line numbers in ``path`` that call the global heap.

    Declaration lines (``= delete``, the escape marker, deleted
    ``operator new``) are not escapes.
    """
    hits = []
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, start=1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("#"):
                continue
            if BAN_DECL_RE.search(line):
                continue
            if HEAP_ESCAPE_RE.search(line):
                hits.append(lineno)
    return hits


def _find_cxx():
    """Return a C++ compiler (c++/g++/clang++) or None when absent."""
    for candidate in ("c++", "g++", "clang++"):
        if shutil.which(candidate) is not None:
            return candidate
    return None


_MISSING_COMPILER_SKIP = (
    "SKIP: no C++ compiler found (c++/g++/clang++ missing) — "
    "live Arena compile-and-run cannot run here "
    "(fail closed, no silent pass)"
)

_ARENA_LIVE_MAIN = r"""
#include <cstdint>
#include <cstdio>
#include "arena.h"

int main() {
  using muse::core::Arena;
  int failures = 0;
  auto check = [&](bool cond, const char* name) {
    if (cond) {
      std::printf("PASS: %s\n", name);
    } else {
      std::printf("FAIL: %s\n", name);
      ++failures;
    }
  };
  // Over-capacity request must be denied with nullptr.
  {
    Arena arena(8);
    check(arena.allocate(9, 1) == nullptr,
          "over-capacity returns nullptr");
  }
  // Padding-exceeds-remaining (shipped BLOCKER shape): after filling 15
  // of 16 bytes, a 1-byte request at 32-byte alignment must be denied
  // with nullptr whenever the alignment padding exceeds the 1 remaining
  // byte — never an out-of-bounds pointer. The expectation is computed
  // from a reference bump model (never from the implementation), so it
  // holds for every possible malloc base alignment.
  {
    Arena arena(16);
    void* first = arena.allocate(15, 1);
    check(first != nullptr, "fill-then-pad setup allocation succeeds");
    const uintptr_t base = reinterpret_cast<uintptr_t>(first);
    const size_t pad = (32u - ((base + 15u) % 32u)) % 32u;
    const bool expect_null = (pad >= 1);  // 15 + pad + 1 > 16 iff pad >= 1
    void* second = arena.allocate(1, 32);
    check((second == nullptr) == expect_null,
          "padding-exceeds-remaining returns nullptr");
  }
  // Reference-model sweep: for every fill of small arenas and for
  // probe alignments 32 and 64, a 1-byte request must return nullptr
  // exactly when fill + padding + 1 exceeds capacity, and otherwise
  // return exactly base + fill + padding. The expectation is computed
  // from a reference bump model (never from the implementation), so a
  // padding value that exceeds the remaining bytes can never be
  // confused with a fit: the unsigned-underflow BLOCKER (padding >
  // remaining wraps and returns an out-of-bounds pointer) is observed
  // as a non-null return where the model demands nullptr. Pairing two
  // capacities (16, 40) with two probe alignments (32, 64) defeats the
  // resonance that lets one capacity hide one malloc-base residue, so
  // the sweep is red against the pre-fix implementation on ordinary
  // 16-aligned malloc bases.
  {
    bool model_ok = true;
    const size_t kAligns[] = {32, 64};
    const size_t kCapacities[] = {16, 40};
    for (size_t ai = 0; ai < 2; ++ai) {
      for (size_t ci = 0; ci < 2; ++ci) {
        const size_t capacity = kCapacities[ci];
        const size_t align = kAligns[ai];
        for (size_t fill = 1; fill <= capacity; ++fill) {
          Arena arena(capacity);
          void* first = arena.allocate(fill, 1);
          if (first == nullptr) {
            // fill <= capacity with align 1 always fits; anything else
            // is itself a denial bug.
            std::printf("FAIL: model setup fill %u/%u denied\n",
                        static_cast<unsigned>(fill),
                        static_cast<unsigned>(capacity));
            model_ok = false;
            continue;
          }
          const uintptr_t base = reinterpret_cast<uintptr_t>(first);
          const size_t pad = (align - ((base + fill) % align)) % align;
          const bool expect_null = (fill + pad + 1 > capacity);
          void* got = arena.allocate(1, align);
          if (expect_null) {
            if (got != nullptr) {
              std::printf("FAIL: model cap=%u align=%u fill=%u pad=%u: "
                          "expected nullptr, got out-of-bounds pointer\n",
                          static_cast<unsigned>(capacity),
                          static_cast<unsigned>(align),
                          static_cast<unsigned>(fill),
                          static_cast<unsigned>(pad));
              model_ok = false;
            }
          } else if (got != reinterpret_cast<void*>(base + fill + pad)) {
            std::printf("FAIL: model cap=%u align=%u fill=%u pad=%u: "
                        "wrong in-bounds pointer\n",
                        static_cast<unsigned>(capacity),
                        static_cast<unsigned>(align),
                        static_cast<unsigned>(fill),
                        static_cast<unsigned>(pad));
            model_ok = false;
          }
        }
      }
    }
    check(model_ok, "padding reference model holds (capacities 16/40)");
  }
  // Exact-fit request must succeed; the arena is then exhausted.
  {
    Arena arena(16);
    check(arena.allocate(16, 1) != nullptr, "exact-fit succeeds");
    check(arena.allocate(1, 1) == nullptr,
          "arena exhausted after exact fit");
  }
  // Non-power-of-two alignment must be rejected, never misaligned.
  {
    Arena arena(16);
    check(arena.allocate(1, 3) == nullptr,
          "non-power-of-two alignment returns nullptr");
  }
  // Sanity: normally aligned allocations succeed.
  {
    Arena arena(16);
    void* first = arena.allocate(8, 8);
    void* second = arena.allocate(8, 8);
    check(first != nullptr && second != nullptr,
          "aligned allocations succeed");
  }
  if (failures == 0) {
    std::puts("ARENA_LIVE_ALL_PASS");
  }
  return failures == 0 ? 0 : 1;
}
"""


def _read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


class MemFilesExist(unittest.TestCase):
    def test_arena_header_exists(self):
        self.assertTrue(os.path.isfile(ARENA_H), "engine/core/mem/arena.h must exist")

    def test_arena_source_exists(self):
        self.assertTrue(os.path.isfile(ARENA_CPP), "engine/core/mem/arena.cpp must exist")

    def test_pool_header_exists(self):
        self.assertTrue(os.path.isfile(POOL_H), "engine/core/mem/pool.h must exist")

    def test_render_alloc_header_exists(self):
        self.assertTrue(
            os.path.isfile(RENDER_ALLOC_H),
            "engine/core/mem/render_alloc.h must exist",
        )


class ArenaContract(unittest.TestCase):
    def test_arena_constructor_capacity(self):
        text = _read(ARENA_H)
        self.assertIn("Arena", text)
        self.assertIn("capacity", text)
        self.assertIn("size_t", text)

    def test_arena_allocate_noexcept(self):
        text = _read(ARENA_H)
        self.assertTrue(
            re.search(r"allocate\s*\(\s*size_t", text),
            "arena.h must declare allocate(size_t n, size_t align)",
        )
        self.assertIn("align", text)
        self.assertIn("noexcept", text)

    def test_arena_reset_noexcept(self):
        text = _read(ARENA_H)
        self.assertTrue(
            re.search(r"reset\s*\(\s*\)\s*noexcept", text),
            "arena.h must declare void reset() noexcept",
        )

    def test_arena_allocate_returns_pointer(self):
        text = _read(ARENA_H)
        self.assertTrue(
            re.search(r"void\s*\*\s*allocate", text),
            "allocate must return void*",
        )


class PoolContract(unittest.TestCase):
    def test_pool_acquire_noexcept(self):
        text = _read(POOL_H)
        self.assertIn("Pool", text)
        self.assertTrue(
            re.search(r"acquire\s*\(\s*\)\s*noexcept", text),
            "pool.h must declare acquire() noexcept",
        )

    def test_pool_release(self):
        text = _read(POOL_H)
        self.assertTrue(
            re.search(r"release\s*\(", text),
            "pool.h must declare release(T*)",
        )

    def test_pool_presized_capacity(self):
        text = _read(POOL_H)
        self.assertTrue(
            ("<T" in text and re.search(r",\s*N\s*>|size_t\s+N|N\b", text))
            or ("N" in text and "Pool" in text),
            "Pool must be pre-sized (Pool<T,N> capacity parameter)",
        )


class RenderAllocBan(unittest.TestCase):
    def test_render_alloc_deletes_global_new(self):
        text = _read(RENDER_ALLOC_H)
        self.assertIn("RenderAlloc", text)
        self.assertTrue(
            re.search(r"operator\s+new.*=\s*delete", text, re.DOTALL),
            "render_alloc.h must delete operator new on the render path",
        )

    def test_heap_escape_marker(self):
        found = False
        for path in (ARENA_H, ARENA_CPP, POOL_H, RENDER_ALLOC_H):
            if os.path.isfile(path) and "CORE_ALLOC_HEAP_ESCAPE" in _read(path):
                found = True
        self.assertTrue(
            found,
            "instrumented builds must log CORE_ALLOC_HEAP_ESCAPE on heap escape",
        )

    def test_render_alloc_headers_have_no_heap_escape(self):
        for path in (ARENA_H, POOL_H, RENDER_ALLOC_H):
            with self.subTest(path=path):
                self.assertEqual(
                    [],
                    scan_heap_escapes(path),
                    "render-path headers must not call the global heap",
                )


class EscapeScannerDenialCases(unittest.TestCase):
    def _scan_text(self, text):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "render_tu.cpp")
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(text)
            return scan_heap_escapes(path)

    def test_heap_allocation_on_render_path_must_fail(self):
        hits = self._scan_text(
            "void* frame() {\n  return malloc(64);\n}\n"
        )
        self.assertTrue(hits, "malloc on the render path must be flagged")

    def test_global_new_on_render_path_must_fail(self):
        hits = self._scan_text("auto* p = new FrameObject();\n")
        self.assertTrue(hits, "global new on the render path must be flagged")

    def test_arena_allocation_must_pass(self):
        hits = self._scan_text(
            "void* frame(Arena& a) {\n  return a.allocate(64, 8);\n}\n"
        )
        self.assertEqual([], hits)

    def test_pool_acquire_must_pass(self):
        hits = self._scan_text(
            "auto* o = pool.acquire();\npool.release(o);\n"
        )
        self.assertEqual([], hits)

    def test_deleted_new_declaration_is_not_an_escape(self):
        hits = self._scan_text(
            "struct RenderAlloc {\n"
            "  static void* operator new(size_t) = delete;\n"
            "};\n"
        )
        self.assertEqual([], hits)


class ArenaLiveBehavior(unittest.TestCase):
    """Live behavioral tests: compile-and-run ``arena.cpp`` (T2 test gap).

    Mirrors the T6 ``c++`` harness pattern: compile the production
    ``arena.cpp`` plus a small driver with ``-std=c++20`` and run it.
    When no compiler (``c++``/``g++``/``clang++``) is present the tests
    fail closed with SKIP naming the missing compiler — never a silent
    pass. Hermetic: toolchain + tmp dir only.
    """

    @classmethod
    def setUpClass(cls):
        cls._tmp = None
        cls._exe = None
        cls._build_stderr = ""
        cxx = _find_cxx()
        if cxx is None:
            return
        cls._tmp = tempfile.TemporaryDirectory()
        src = os.path.join(cls._tmp.name, "arena_live_main.cpp")
        cls._exe = os.path.join(cls._tmp.name, "arena_live")
        with open(src, "w", encoding="utf-8") as fh:
            fh.write(_ARENA_LIVE_MAIN)
        proc = subprocess.run(
            [cxx, "-std=c++20", "-I", MEM_ROOT, src, ARENA_CPP,
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

    def _run_live(self):
        if _find_cxx() is None or self._exe is None:
            if self._exe is None and _find_cxx() is not None:
                self.fail(
                    "live Arena driver must compile: %s"
                    % (self._build_stderr,)
                )
            self.skipTest(_MISSING_COMPILER_SKIP)
        proc = subprocess.run(
            [self._exe], capture_output=True, text=True, timeout=60
        )
        self.assertEqual(
            proc.returncode,
            0,
            "live Arena driver must exit 0 (no crash, all cases hold):\n%s"
            % (proc.stdout + proc.stderr,),
        )
        return proc.stdout

    def test_live_overflow_returns_nullptr(self):
        out = self._run_live()
        self.assertIn("PASS: over-capacity returns nullptr", out)

    def test_live_padding_exceeds_remaining_returns_nullptr(self):
        out = self._run_live()
        self.assertIn(
            "PASS: padding-exceeds-remaining returns nullptr", out
        )
        self.assertIn(
            "PASS: padding reference model holds (capacities 16/40)", out
        )

    def test_live_exact_fit_succeeds(self):
        out = self._run_live()
        self.assertIn("PASS: exact-fit succeeds", out)
        self.assertIn("PASS: arena exhausted after exact fit", out)

    def test_live_non_power_of_two_alignment_rejected(self):
        out = self._run_live()
        self.assertIn(
            "PASS: non-power-of-two alignment returns nullptr", out
        )

    def test_live_aligned_allocations_succeed(self):
        out = self._run_live()
        self.assertIn("PASS: aligned allocations succeed", out)
        self.assertIn("ARENA_LIVE_ALL_PASS", out)


if __name__ == "__main__":
    unittest.main()
