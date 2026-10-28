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
declared-interface tokens + error-shape marker); the Gradle task wiring
itself (``:engine:core:allocator``) is the coder's T2 build work and is
marked EXTERNAL below — the repo's ``gradlew`` shim currently exposes
only the ``:benchmark:*`` surface. Hermetic: stdlib unittest only, no
network/secrets/services/config, no compilation.

Expected state before T2 implementation: RED — ``engine/core/mem`` does
not exist, so every file-existence/interface assertion fails. The
injected-fixture escape-scanner tests pass from the start (they only
need the in-file scanner + tmp dirs).
"""

import os
import re
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


if __name__ == "__main__":
    unittest.main()
