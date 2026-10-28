// No-dynamic-initializer startup-path check (NFR-CORE-004, AC-CORE-031).
//
// Declares the T2 `Interface:` line from plan.md (verbatim):
//   `int check_dynamic_initializers(const char* object_dir);`
// returning `0` when no dynamic initializer is present on the startup path
// and nonzero plus `DYNAMIC_INIT:<file>:<symbol>` lines otherwise.
//
// Fail-closed: a null path, a missing/unreadable directory, or any I/O
// failure returns nonzero and maps to the FR-PKG-005 fail-closed shape
// (a denial, never a silent pass). The function never throws.
#pragma once

namespace muse {
namespace startup {

// Scan `object_dir` (recursively) for startup-path objects carrying
// dynamic initializers. Prints one `DYNAMIC_INIT:<file>:<symbol>` line per
// violation to stdout. Returns 0 when clean, 1 when a violation is found,
// 2 when the input cannot be scanned (null path, missing/unreadable dir,
// I/O failure). Never throws.
int check_dynamic_initializers(const char* object_dir) noexcept;

}  // namespace startup
}  // namespace muse
