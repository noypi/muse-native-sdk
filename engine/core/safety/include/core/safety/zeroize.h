// Secure zeroization helper (NFR-CORE-005 / AC-CORE-006,
// NFR-CORE-011 / AC-CORE-029).
//
// Overwrites [ptr, ptr+len) with zeros through a compiler-opaque
// write (explicit_bzero-style; never elided).
//
// Semantics: null with nonzero len is a tolerated no-op (not
// trapped); zero len is a no-op (buffer untouched, even for a
// non-null pointer).
//
// Portability: where the platform provides memset_s (C11 Annex K)
// or explicit_bzero (glibc/BSD, Android NDK), those could be used
// as the opaque write. This translation unit uses the portable
// volatile-loop fallback so behaviour is identical on every
// toolchain, including NDK builds without those symbols.
//
// RAII-guard note: callers holding key material should wrap the
// buffer in a small RAII guard whose destructor calls
// muse_core_safety_zeroize() before release, so early returns and
// exceptions cannot skip the wipe. No guard is defined here in
// order to keep this header engine-type-free.
//
// Conventions: C++20, hidden visibility, -Wall -Wextra -Wpedantic
// clean. Global namespace; header depends only on <cstddef>.
#pragma once

#include <cstddef>

void muse_core_safety_zeroize(void* ptr, std::size_t len) noexcept;
