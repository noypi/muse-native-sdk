// Batch 3 contract tests — T5 core-side sandbox: OTA signature
// verification gate + W^X enforcement.
// Specs: NFR-CORE-008 (primitive), NFR-SECURITY-008 / AC-SECURITY-009
// (verify-before-execute against pinned keys, no unsigned override),
// AC-SECURITY-010 (W^X, no RWX pages), AC-SECURITY-011 (tampered /
// expired / untrusted-key rejected pre-execution with logging),
// FR-PKG-005 (fail-closed `SandboxError{ code, check, file, expected }`
// shape, maps/loads nothing on denial), NFR-CORE-003 (loader.h trust:
// plugin .so paths accepted only post-verification).
//
// Binds to the T5 `Interface:` line in plan.md (verbatim):
//   `VerifyResult verifyAndMap(const BundleRef& bundle, const KeySet&
//   pinnedKeys) noexcept;` returning `ok(mapping)` only after signature
//   verification against pinned keys and with executable pages mapped
//   read-execute (never read-write-execute); every denial returns
//   `err(SandboxError{ code, check, file, expected })` and maps/loads
//   nothing (fail-closed per FR-PKG-005 shape).
//
// Hermetic: C++20 + `core/sandbox/verifier.h` only. No real keys, no
// key storage, no filesystem, no sockets, no live services, no
// wall-clock reads in the test itself (expiry is carried inline in the
// BundleRef fixture, never read from the OS clock).
//
// ASSUMPTIONS (contract genuinely silent on these details):
//   A4-keys: namespace is `muse::sandbox`; `KeySet` is an already-pinned
//     input (core never stores keys — plan A4); the test builds the
//     pinned set inline from obvious dummy key ids (`"test-key-a"`,
//     `"test-key-b"`), never real keys.
//   A-sandbox-1: `BundleRef` exposes `{ path, bytes, size, signature,
//     signatureSize, expiryUnixSec, keyId }` (aggregate-constructible);
//     `KeySet` exposes `{ keyIds, count }` over `const char*` ids.
//   A-sandbox-2: `Mapping` exposes `{ valid, base, size, executable,
//     writable }`; denial means `ok=false` with `mapping.valid=false`
//     (nothing mapped); success means `ok=true`, `mapping.valid=true`,
//     `executable=true`, `writable=false` (RX-only, never RWX).
//   A-sandbox-3: `SandboxError` exposes `{ code, check, file, expected }`
//     as `{ int code; const char* check; const char* file; const char*
//     expected; }` with `check` naming the distinct reason
//     (`"signature-integrity"`, `"signature-expiry"`, `"key-trust"`)
//     and `file` echoing the bundle path.
//   A-sandbox-4: `VerifyResult` exposes `{ bool ok; Mapping mapping;
//     SandboxError error; }` with `error.code == 0` on success.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "core/sandbox/verifier.h"

namespace {

using muse::sandbox::BundleRef;
using muse::sandbox::KeySet;
using muse::sandbox::verifyAndMap;
using muse::sandbox::VerifyResult;

int g_failed_checks = 0;

#define CHECK(cond)                                \
  do {                                             \
    if (!(cond)) {                                 \
      ++g_failed_checks;                           \
      std::printf("    CHECK failed line %d: %s\n", \
                  __LINE__, #cond);               \
    }                                              \
  } while (0)

// --- Inline fixtures: obvious dummy values only, never real keys. ---

uint8_t g_bytes_valid[] = {0x7F, 0x45, 0x4C, 0x46, 0x01, 0x02, 0x03, 0x04};
uint8_t g_bytes_tampered[] = {0x7F, 0x45, 0x4C, 0x46, 0x01, 0x02, 0x03, 0xFF};
uint8_t g_sig[] = {0xDE, 0xAD, 0xBE, 0xEF};

const char* kPinnedIds[] = {"test-key-a", "test-key-b"};
const char* kUntrustedId = "untrusted-key-not-pinned";

// Far-future expiry carried inline in the fixture (no OS clock read).
constexpr uint64_t kValidExpiry = 4102444800ULL;  // 2100-01-01.
// Long-past expiry carried inline in the fixture.
constexpr uint64_t kExpiredExpiry = 946684800ULL;  // 2000-01-01.

KeySet pinnedKeys() {
  KeySet ks{};
  ks.keyIds = kPinnedIds;
  ks.count = 2;
  return ks;
}

BundleRef validBundle() {
  BundleRef b{};
  b.path = "test-bundle/valid.ota";
  b.bytes = g_bytes_valid;
  b.size = sizeof(g_bytes_valid);
  b.signature = g_sig;
  b.signatureSize = sizeof(g_sig);
  b.expiryUnixSec = kValidExpiry;
  b.keyId = "test-key-a";
  return b;
}

bool check_signature_is_noexcept() {
  CHECK((std::is_nothrow_invocable_v<decltype(&verifyAndMap), const BundleRef&,
                                    const KeySet&>));
  return g_failed_checks == 0;
}

bool check_tampered_byte_rejected_pre_execution() {
  // AC-SECURITY-011: modified-byte bundle rejected before execution with
  // a distinct named reason, and nothing mapped (fail-closed FR-PKG-005).
  BundleRef b = validBundle();
  b.path = "test-bundle/tampered.ota";
  b.bytes = g_bytes_tampered;
  b.size = sizeof(g_bytes_tampered);
  VerifyResult r = verifyAndMap(b, pinnedKeys());
  CHECK(!r.ok);
  if (r.ok) return false;
  CHECK(r.error.code != 0);
  CHECK(r.error.check != nullptr &&
        std::strcmp(r.error.check, "signature-integrity") == 0);
  CHECK(r.error.file != nullptr &&
        std::strcmp(r.error.file, "test-bundle/tampered.ota") == 0);
  CHECK(r.error.expected != nullptr && r.error.expected[0] != '\0');
  CHECK(!r.mapping.valid);
  CHECK(r.mapping.base == nullptr);
  CHECK(r.mapping.size == 0);
  return g_failed_checks == 0;
}

bool check_expired_signature_rejected_pre_execution() {
  // AC-SECURITY-011: expired-signature bundle rejected before execution
  // with a distinct named reason, and nothing mapped.
  BundleRef b = validBundle();
  b.path = "test-bundle/expired.ota";
  b.expiryUnixSec = kExpiredExpiry;
  VerifyResult r = verifyAndMap(b, pinnedKeys());
  CHECK(!r.ok);
  if (r.ok) return false;
  CHECK(r.error.code != 0);
  CHECK(r.error.check != nullptr &&
        std::strcmp(r.error.check, "signature-expiry") == 0);
  CHECK(r.error.file != nullptr &&
        std::strcmp(r.error.file, "test-bundle/expired.ota") == 0);
  CHECK(r.error.expected != nullptr && r.error.expected[0] != '\0');
  CHECK(!r.mapping.valid);
  CHECK(r.mapping.base == nullptr);
  CHECK(r.mapping.size == 0);
  return g_failed_checks == 0;
}

bool check_untrusted_key_rejected_pre_execution() {
  // AC-SECURITY-009: bundle signed by a key outside the pinned set is
  // rejected (no unsigned override); distinct named reason, nothing
  // mapped.
  BundleRef b = validBundle();
  b.path = "test-bundle/untrusted-key.ota";
  b.keyId = kUntrustedId;
  VerifyResult r = verifyAndMap(b, pinnedKeys());
  CHECK(!r.ok);
  if (r.ok) return false;
  CHECK(r.error.code != 0);
  CHECK(r.error.check != nullptr &&
        std::strcmp(r.error.check, "key-trust") == 0);
  CHECK(r.error.file != nullptr &&
        std::strcmp(r.error.file, "test-bundle/untrusted-key.ota") == 0);
  CHECK(r.error.expected != nullptr && r.error.expected[0] != '\0');
  CHECK(!r.mapping.valid);
  return g_failed_checks == 0;
}

bool check_denial_reasons_are_distinct() {
  // Each denial class carries its own named check so callers (and the
  // AC-SECURITY-011 log) can distinguish them.
  BundleRef tampered = validBundle();
  tampered.bytes = g_bytes_tampered;
  tampered.size = sizeof(g_bytes_tampered);
  BundleRef expired = validBundle();
  expired.expiryUnixSec = kExpiredExpiry;
  BundleRef untrusted = validBundle();
  untrusted.keyId = kUntrustedId;
  VerifyResult r1 = verifyAndMap(tampered, pinnedKeys());
  VerifyResult r2 = verifyAndMap(expired, pinnedKeys());
  VerifyResult r3 = verifyAndMap(untrusted, pinnedKeys());
  CHECK(!r1.ok && !r2.ok && !r3.ok);
  if (r1.ok || r2.ok || r3.ok) return false;
  CHECK(std::strcmp(r1.error.check, r2.error.check) != 0);
  CHECK(std::strcmp(r1.error.check, r3.error.check) != 0);
  CHECK(std::strcmp(r2.error.check, r3.error.check) != 0);
  return g_failed_checks == 0;
}

bool check_valid_bundle_verifies_rx_only() {
  // Happy path: valid signed bundle verifies against pinned keys and
  // maps read-execute, never read-write-execute (AC-SECURITY-010).
  BundleRef b = validBundle();
  VerifyResult r = verifyAndMap(b, pinnedKeys());
  CHECK(r.ok);
  if (!r.ok) return false;
  CHECK(r.error.code == 0);
  CHECK(r.mapping.valid);
  CHECK(r.mapping.base != nullptr);
  CHECK(r.mapping.size > 0);
  CHECK(r.mapping.executable);
  CHECK(!r.mapping.writable);
  CHECK(!(r.mapping.executable && r.mapping.writable));
  return g_failed_checks == 0;
}

bool check_zero_rwx_pages_on_success() {
  // W^X self-check (AC-SECURITY-010): no RWX pages on a verified
  // mapping. Single-page mapping here, so the conjunction must be false.
  BundleRef b = validBundle();
  VerifyResult r = verifyAndMap(b, pinnedKeys());
  CHECK(r.ok);
  if (!r.ok) return false;
  const bool rwx = r.mapping.executable && r.mapping.writable;
  CHECK(!rwx);
  return g_failed_checks == 0;
}

}  // namespace

int main() {
  int failed = 0;
  auto run = [&](const char* name, bool (*fn)()) {
    g_failed_checks = 0;
    std::printf("TINYTEST %s\n", name);
    if (!fn()) {
      ++failed;
      std::printf("  FAIL %s (%d checks failed)\n", name, g_failed_checks);
    } else {
      std::printf("  PASS %s\n", name);
    }
  };
  run("signature_is_noexcept", check_signature_is_noexcept);
  run("tampered_byte_rejected_pre_execution",
      check_tampered_byte_rejected_pre_execution);
  run("expired_signature_rejected_pre_execution",
      check_expired_signature_rejected_pre_execution);
  run("untrusted_key_rejected_pre_execution",
      check_untrusted_key_rejected_pre_execution);
  run("denial_reasons_are_distinct", check_denial_reasons_are_distinct);
  run("valid_bundle_verifies_rx_only", check_valid_bundle_verifies_rx_only);
  run("zero_rwx_pages_on_success", check_zero_rwx_pages_on_success);
  if (failed > 0) {
    std::printf("RESULT FAIL %d/7 cases failed\n", failed);
    return 1;
  }
  std::printf("RESULT PASS 7/7 cases\n");
  return 0;
}
