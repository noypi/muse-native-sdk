// Batch 4 (Server-Driven UI, FR-STATE-009) contract test — T9.
// Spec refs: FR-STATE-009, AC-STATE-037 (TC-STATE-037). Assumption A2:
// descriptor newer than client fails soft (kCorrupt → bundled fallback).
// Hermetic: C++17 + committed engine/state headers only; fake verifier key
// in-test, never a real Keystore; no env/config/network.
// Binds ONLY to the T9 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T9.md):
//   A-V1: namespace muse::state; types live in `muse/state/descriptor.hpp`,
//         `verifyDescriptor` defined in `serverui/src/verify.cpp`.
//   A-V2: DescriptorBytes exposes `static DescriptorBytes from(const char*,
//         std::size_t)` (or equivalent factory) plus `empty() const`;
//         Descriptor exposes `std::uint64_t version() const`.
//   A-V3: Test-only signing helper `DescriptorBytes signTestOnly(const char*
//         payload, const char* keyId, std::uint64_t validUntil,
//         std::uint64_t version)` lives in the same header (fake verifier key,
//         never real crypto); production verifies against TrustAnchor.
//   A-V4: kClientVersion is a header constant (or verifyDescriptor treats any
//         version greater than the bundled max as newer-than-client).
//   A-V5: Null keyId / empty bytes / malformed bytes return kCorrupt, never
//         throw; expired means validUntil < fake now supplied in-test via
//         `setTestOnlyNow(std::uint64_t)`.
#include <cstdio>
#include <cstdint>

#include "muse/state/atom.hpp"
#include "muse/state/descriptor.hpp"

namespace {

using muse::state::Descriptor;
using muse::state::DescriptorBytes;
using muse::state::Status;
using muse::state::TrustAnchor;

static TrustAnchor anchor() { return TrustAnchor{"test-key-1", 9000}; }

// Signed descriptor with valid key + unexpired anchor verifies kOk.
static bool check_valid_signed_descriptor_verifies() {
  DescriptorBytes b =
      muse::state::signTestOnly("screen:home", "test-key-1", 9000, 1);
  auto r = muse::state::verifyDescriptor(b, anchor());
  return r.ok();
}

// Unsigned (plain payload, no signature envelope) is kCorrupt.
static bool check_unsigned_descriptor_rejected() {
  DescriptorBytes b = DescriptorBytes::from("screen:home", 11);
  auto r = muse::state::verifyDescriptor(b, anchor());
  return !r.ok();
}

// Mis-signed (wrong key id) is kCorrupt → caller uses bundled screen.
static bool check_mis_signed_descriptor_rejected() {
  DescriptorBytes b =
      muse::state::signTestOnly("screen:home", "attacker-key", 9000, 1);
  auto r = muse::state::verifyDescriptor(b, anchor());
  return !r.ok();
}

// Expired anchor (validUntil < now) is kCorrupt.
static bool check_expired_descriptor_rejected() {
  muse::state::setTestOnlyNow(10000);
  DescriptorBytes b =
      muse::state::signTestOnly("screen:home", "test-key-1", 9000, 1);
  auto r = muse::state::verifyDescriptor(b, anchor());
  muse::state::setTestOnlyNow(0);
  return !r.ok();
}

// Newer-than-client version fails soft to kCorrupt (A2), never crash.
static bool check_newer_than_client_version_fails_soft() {
  DescriptorBytes b = muse::state::signTestOnly(
      "screen:home", "test-key-1", 9000, muse::state::kClientVersion + 1);
  auto r = muse::state::verifyDescriptor(b, anchor());
  return !r.ok();
}

// Empty bytes are kCorrupt.
static bool check_empty_bytes_rejected() {
  DescriptorBytes b = DescriptorBytes::from("", 0);
  auto r = muse::state::verifyDescriptor(b, anchor());
  return !r.ok();
}

// Null keyId anchor is kCorrupt, never throws.
static bool check_null_key_anchor_rejected() {
  DescriptorBytes b =
      muse::state::signTestOnly("screen:home", "test-key-1", 9000, 1);
  TrustAnchor bad{nullptr, 9000};
  auto r = muse::state::verifyDescriptor(b, bad);
  return !r.ok();
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_valid_signed_descriptor_verifies);
  RUN_CASE(check_unsigned_descriptor_rejected);
  RUN_CASE(check_mis_signed_descriptor_rejected);
  RUN_CASE(check_expired_descriptor_rejected);
  RUN_CASE(check_newer_than_client_version_fails_soft);
  RUN_CASE(check_empty_bytes_rejected);
  RUN_CASE(check_null_key_anchor_rejected);
  if (failures > 0) {
    std::printf("descriptor_signature_test: %d failing\n", failures);
    return 1;
  }
  std::printf("descriptor_signature_test: all passing\n");
  return 0;
}
