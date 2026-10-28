// Batch 1 contract tests — T2 secure zeroization helper
// (NFR-CORE-005 / AC-CORE-006; NFR-CORE-011 / AC-CORE-029; TC-CORE-029).
//
// Hermetic: C++20 + engine/core/safety headers only. No env/config
// reads, no filesystem, no sockets, no live services, no wall-clock.
// All secret buffers are inline stack/heap arrays filled with dummy
// patterns; nothing here is a real key or credential.
//
// Binds to the T2 `Interface:` line in plan.md:
//   void muse_core_safety_zeroize(void* ptr, std::size_t len) noexcept
// overwrites [ptr, ptr+len) with zeros through a compiler-opaque
// write (explicit_bzero-style; never elided). Error/denial shape:
// none (noexcept, no return); misuse (null+nonzero is tolerated,
// not trapped).
//
// ASSUMPTIONS:
//   A-T2-1: the helper lives in the global namespace (the plan names
//     it `muse_core_safety_zeroize`, not `muse::...`), so tests call
//     it unqualified at global scope.
//   A-T2-2: "never elided" is verified disassembly-insensitively: the
//     test reads every byte back through a volatile observer after
//     the call, so a compiler that elided the write would fail the
//     read-back assertions under -O2. No objdump/nm inspection.
//   A-T2-3: null+nonzero is a tolerated no-op (plan Interface line).
//     Tests assert it does not trap; they do not assert any write.
//   A-T2-4: C++20, hidden visibility, -Wall -Wextra -Wpedantic per
//     plan A4; the test compiles clean under those flags.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

#include "core/safety/zeroize.h"

namespace {

int g_failed_checks = 0;

#define CHECK(cond)                              \
  do {                                           \
    if (!(cond)) {                               \
      ++g_failed_checks;                         \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);             \
    }                                            \
  } while (0)

// Volatile observer: forces the compiler to treat the buffer contents
// as observable after zeroization, defeating dead-store elimination
// of the test's own read-back (and would catch an elided helper
// write, since the bytes would still hold the secret pattern).
bool all_zero_observed(const void* ptr, std::size_t len) {
  const volatile auto* p =
      static_cast<const volatile unsigned char*>(ptr);
  for (std::size_t i = 0; i < len; ++i) {
    if (p[i] != 0) return false;
  }
  return true;
}

void fill_pattern(void* ptr, std::size_t len, unsigned char seed) {
  auto* p = static_cast<unsigned char*>(ptr);
  for (std::size_t i = 0; i < len; ++i) {
    p[i] = static_cast<unsigned char>(seed + (i & 0xFF));
  }
}

bool test_signature_is_noexcept_void() {
  // Interface: void (void*, std::size_t) noexcept.
  static_assert(
      std::is_same_v<decltype(muse_core_safety_zeroize(nullptr,
                                                        std::size_t{0})),
                     void>,
      "zeroize must return void");
  static_assert(
      noexcept(muse_core_safety_zeroize(nullptr, std::size_t{0})),
      "zeroize must be noexcept");
  return true;
}

bool test_64_byte_secret_is_wiped() {
  // T2 acceptance: 64-byte non-zero secret buffer -> all bytes zero.
  unsigned char secret[64];
  fill_pattern(secret, sizeof(secret), 0xA5);
  CHECK(!all_zero_observed(secret, sizeof(secret)));
  muse_core_safety_zeroize(secret, sizeof(secret));
  CHECK(all_zero_observed(secret, sizeof(secret)));
  return true;
}

bool test_all_ones_pattern_is_wiped() {
  // Boundary: uniform 0xFF fill (worst case for bit-stuck faults).
  unsigned char buf[32];
  for (auto& b : buf) b = 0xFF;
  muse_core_safety_zeroize(buf, sizeof(buf));
  CHECK(all_zero_observed(buf, sizeof(buf)));
  return true;
}

bool test_single_byte_is_wiped() {
  // Boundary: minimal non-empty length.
  unsigned char b = 0x7E;
  muse_core_safety_zeroize(&b, 1);
  CHECK(b == 0);
  return true;
}

bool test_large_buffer_is_wiped() {
  // Boundary: multi-page-sized buffer, edges + middle sampled.
  constexpr std::size_t kLen = 8192;
  static unsigned char buf[kLen];
  fill_pattern(buf, kLen, 0x3C);
  muse_core_safety_zeroize(buf, kLen);
  CHECK(all_zero_observed(buf, kLen));
  CHECK(buf[0] == 0);
  CHECK(buf[kLen / 2] == 0);
  CHECK(buf[kLen - 1] == 0);
  return true;
}

bool test_zero_length_is_noop() {
  // Boundary: zero length leaves the buffer untouched.
  unsigned char b = 0x5A;
  muse_core_safety_zeroize(&b, 0);
  CHECK(b == 0x5A);
  return true;
}

bool test_null_with_zero_length_is_noop() {
  // Boundary: null + zero length is a tolerated no-op, not a trap.
  muse_core_safety_zeroize(nullptr, 0);
  CHECK(true);
  return true;
}

bool test_null_with_nonzero_length_is_tolerated() {
  // Misuse shape (plan Interface): null+nonzero is tolerated, not
  // trapped. Must return without aborting; no write is asserted.
  muse_core_safety_zeroize(nullptr, 16);
  CHECK(true);
  return true;
}

bool test_already_zero_buffer_stays_zero() {
  // Idempotence edge: zeroing a clean buffer is still clean.
  unsigned char buf[16] = {};
  muse_core_safety_zeroize(buf, sizeof(buf));
  CHECK(all_zero_observed(buf, sizeof(buf)));
  return true;
}

bool test_heap_buffer_wiped_before_release() {
  // AC-CORE-029 shape: secret wiped before the buffer is released.
  // The wipe is observed before delete[]; ASan build must stay clean.
  constexpr std::size_t kLen = 64;
  auto* heap = new unsigned char[kLen];
  fill_pattern(heap, kLen, 0xD2);
  muse_core_safety_zeroize(heap, kLen);
  const bool wiped = all_zero_observed(heap, kLen);
  delete[] heap;
  CHECK(wiped);
  return true;
}

bool test_partial_length_wipes_prefix_only() {
  // Contract edge: only [ptr, ptr+len) is touched; the tail keeps
  // its contents (documents exact-range semantics, no over-wipe).
  unsigned char buf[16];
  fill_pattern(buf, sizeof(buf), 0x11);
  muse_core_safety_zeroize(buf, 8);
  CHECK(all_zero_observed(buf, 8));
  CHECK(!all_zero_observed(buf + 8, 8));
  return true;
}

}  // namespace

int main() {
  struct Case {
    const char* name;
    bool (*fn)();
  };
  const Case cases[] = {
      {"signature_is_noexcept_void", test_signature_is_noexcept_void},
      {"64_byte_secret_is_wiped", test_64_byte_secret_is_wiped},
      {"all_ones_pattern_is_wiped", test_all_ones_pattern_is_wiped},
      {"single_byte_is_wiped", test_single_byte_is_wiped},
      {"large_buffer_is_wiped", test_large_buffer_is_wiped},
      {"zero_length_is_noop", test_zero_length_is_noop},
      {"null_with_zero_length_is_noop",
       test_null_with_zero_length_is_noop},
      {"null_with_nonzero_length_is_tolerated",
       test_null_with_nonzero_length_is_tolerated},
      {"already_zero_buffer_stays_zero",
       test_already_zero_buffer_stays_zero},
      {"heap_buffer_wiped_before_release",
       test_heap_buffer_wiped_before_release},
      {"partial_length_wipes_prefix_only",
       test_partial_length_wipes_prefix_only},
  };
  int failed = 0;
  for (const auto& c : cases) {
    const int before = g_failed_checks;
    c.fn();
    const bool pass = (g_failed_checks == before);
    std::printf("%s %s\n", pass ? "PASS" : "FAIL", c.name);
    if (!pass) ++failed;
  }
  std::printf("%d/%d cases passed\n",
              static_cast<int>(sizeof(cases) / sizeof(cases[0])) - failed,
              static_cast<int>(sizeof(cases) / sizeof(cases[0])));
  return failed == 0 ? 0 : 1;
}
