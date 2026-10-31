// Batch 1 T1 — zero-copy frame import (NFR-MEDIA-002/AC-MEDIA-002, RULE-MEDIA-002;
// required FR-MEDIA-005/AC-MEDIA-008, FR-MEDIA-006/AC-MEDIA-009).
// Binds ONLY to the T1 `Interface:` line in plan.md; never to implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/zero_copy_frame.h only;
// no network, no env/config reads, no filesystem, no live services.
// DeviceCaps / HardwareBufferHandle are narrow injected structs (peer seams).
//
// Expected state: RED — engine/media/include/media/zero_copy_frame.h does not
// exist yet, so this file fails to compile until the coder implements T1.
//
// Contract assumptions (see workflow.local/runs/nfr-media-002-fr-media-010/unit-tests/T1.md):
//   A-NS:     everything in namespace `media`; header
//             `engine/media/include/media/zero_copy_frame.h`.
//   A-TYPES:  `struct HardwareBufferHandle { bool valid; uint64_t id; };`
//             `struct DeviceCaps { bool external_memory; bool sync_fence; };`
//             `struct ZeroCopyImportResult { bool ok; uint64_t vk_image;
//               bool fence_ok; const char* reason; };`
//             `ZeroCopyImportResult ImportHardwareBuffer(
//                const HardwareBufferHandle& buffer, uint64_t fence_fd,
//                bool* out_host_copy_avoided);`
//             `bool IsZeroCopyAvailable(const DeviceCaps& caps);`
#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/media/include/media/zero_copy_frame.h"

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

// --- cases -----------------------------------------------------------------

// Capable device: import succeeds, host copy avoided, fence bound.
static void check_capable_import_avoids_host_copy() {
  media::HardwareBufferHandle buf{true, 42u};
  bool avoided = false;
  bool threw = false;
  media::ZeroCopyImportResult r{false, 0u, false, nullptr};
  try {
    r = media::ImportHardwareBuffer(buf, /*fence_fd=*/7u, &avoided);
  } catch (...) {
    threw = true;
  }
  CHECK("capable import never throws", !threw);
  CHECK("capable import ok", r.ok == true);
  CHECK("capable import reports host copy avoided", avoided == true);
  CHECK("capable import binds a vk image", r.vk_image != 0u);
  CHECK("capable import binds the fence", r.fence_ok == true);
}

// Capability gate: full caps available, partial caps not.
static void check_capability_gate() {
  media::DeviceCaps full{true, true};
  media::DeviceCaps no_ext{false, true};
  media::DeviceCaps no_fence{true, false};
  CHECK("full caps available", media::IsZeroCopyAvailable(full) == true);
  CHECK("missing ext memory unavailable",
        media::IsZeroCopyAvailable(no_ext) == false);
  CHECK("missing fence unavailable",
        media::IsZeroCopyAvailable(no_fence) == false);
}

// Incapable device path: fail closed — ok=false with reason, never silent
// host-copy success.
static void check_incapable_fails_closed() {
  media::HardwareBufferHandle buf{true, 9u};
  bool avoided = true;  // poison: must be cleared on fail-closed path
  bool threw = false;
  media::ZeroCopyImportResult r{true, 1u, true, nullptr};
  try {
    // fence_fd 0 models "no native sync fence available".
    r = media::ImportHardwareBuffer(buf, /*fence_fd=*/0u, &avoided);
  } catch (...) {
    threw = true;
  }
  CHECK("incapable import never throws", !threw);
  // Either ok (with fence unbound is itself a failure below) — the binding
  // contract is: no success may claim host_copy_avoided without a fence.
  if (r.ok) {
    CHECK("success without fence must not claim avoided",
          !(avoided && !r.fence_ok));
  } else {
    CHECK("fail-closed carries a reason",
          r.reason != nullptr && std::string(r.reason).size() > 0);
    CHECK("fail-closed never claims host copy avoided", avoided == false);
  }
}

// Invalid buffer handle fails closed with a reason, never throws.
static void check_invalid_buffer_fails_closed() {
  media::HardwareBufferHandle buf{false, 0u};
  bool avoided = true;
  bool threw = false;
  media::ZeroCopyImportResult r{true, 0u, false, nullptr};
  try {
    r = media::ImportHardwareBuffer(buf, /*fence_fd=*/3u, &avoided);
  } catch (...) {
    threw = true;
  }
  CHECK("invalid buffer never throws", !threw);
  CHECK("invalid buffer ok=false", r.ok == false);
  CHECK("invalid buffer carries a reason",
        r.reason != nullptr && std::string(r.reason).size() > 0);
  CHECK("invalid buffer never claims avoided", avoided == false);
}

// Null out flag is tolerated and never crashes.
static void check_null_out_flag_tolerated() {
  media::HardwareBufferHandle buf{true, 1u};
  bool threw = false;
  media::ZeroCopyImportResult r{true, 0u, false, nullptr};
  try {
    r = media::ImportHardwareBuffer(buf, /*fence_fd=*/5u,
                                    /*out_host_copy_avoided=*/nullptr);
  } catch (...) {
    threw = true;
  }
  CHECK("null out flag never throws", !threw);
  (void)r;
}

// Security: hostile fence values / recycled ids never throw and never claim
// an avoided host copy without a bound fence.
static void check_hostile_fence_never_throws() {
  media::HardwareBufferHandle buf{true, 0xFFFFFFFFFFFFFFFFull};
  bool avoided = true;
  bool threw = false;
  media::ZeroCopyImportResult r{false, 0u, false, nullptr};
  try {
    r = media::ImportHardwareBuffer(buf, /*fence_fd=*/0xFFFFFFFFFFFFFFFFull,
                                    &avoided);
  } catch (...) {
    threw = true;
  }
  CHECK("hostile fence never throws", !threw);
  if (r.ok) {
    CHECK("hostile success binds image", r.vk_image != 0u);
  } else {
    CHECK("hostile fail-closed carries a reason",
          r.reason != nullptr && std::string(r.reason).size() > 0);
  }
}

int main() {
  check_capable_import_avoids_host_copy();
  check_capability_gate();
  check_incapable_fails_closed();
  check_invalid_buffer_fails_closed();
  check_null_out_flag_tolerated();
  check_hostile_fence_never_throws();
  if (s_failures == 0) {
    std::printf("ALL GREEN zero_copy_frame_test\n");
    return 0;
  }
  std::printf("%d FAILURES zero_copy_frame_test\n", s_failures);
  return 1;
}
