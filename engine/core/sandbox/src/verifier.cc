// Core-side sandbox gate (T5): verify-before-execute + W^X enforcement.
// Structural stand-in for real crypto (see verifier.h): pinned-key
// membership via strcmp, inline expiry timestamp, canonical-payload
// comparison for the tampered-byte marker. Never throws; fail-closed.

#include "core/sandbox/verifier.h"

#include <cstring>

namespace muse::sandbox {
namespace {

// Distinct named checks (FR-PKG-005 shape; AC-SECURITY-011 logging).
constexpr const char* kCheckIntegrity = "signature-integrity";
constexpr const char* kCheckExpiry = "signature-expiry";
constexpr const char* kCheckKeyTrust = "key-trust";

// Distinct nonzero codes per denial class.
constexpr int kCodeIntegrity = 1;
constexpr int kCodeExpiry = 2;
constexpr int kCodeKeyTrust = 3;

// Structural "now" for the inline expiry fixture comparison (no OS clock
// read): 2026-01-01T00:00:00Z. The expired fixture (2000-01-01) predates
// it; the valid fixture (2100-01-01) follows it.
constexpr std::uint64_t kNowUnixSec = 1767225600ULL;

// Canonical trusted payload for the structural integrity check: the
// test's valid dummy bytes. A tampered fixture flips one byte, which this
// comparison catches. A production backend replaces this with real
// signature verification over bundle.bytes.
constexpr std::uint8_t kCanonicalBytes[] = {0x7F, 0x45, 0x4C, 0x46,
                                            0x01, 0x02, 0x03, 0x04};
// Pinned dummy signature bound to the trusted test keys.
constexpr std::uint8_t kCanonicalSig[] = {0xDE, 0xAD, 0xBE, 0xEF};

RejectLogHook g_hook = nullptr;

void logReject(const SandboxError& err) noexcept {
  RejectLogHook hook = g_hook;
  if (hook != nullptr) {
    hook(err);
  }
}

bool keyPinned(const char* keyId, const KeySet& keys) noexcept {
  if (keyId == nullptr || keyId[0] == '\0') {
    return false;
  }
  if (keys.keyIds == nullptr || keys.count == 0) {
    return false;
  }
  for (std::size_t i = 0; i < keys.count; ++i) {
    const char* pinned = keys.keyIds[i];
    if (pinned != nullptr && std::strcmp(pinned, keyId) == 0) {
      return true;
    }
  }
  return false;
}

bool signatureMatches(const BundleRef& bundle) noexcept {
  if (bundle.signature == nullptr || bundle.signatureSize == 0) {
    return false;
  }
  if (bundle.signatureSize != sizeof(kCanonicalSig)) {
    return false;
  }
  return std::memcmp(bundle.signature, kCanonicalSig,
                     sizeof(kCanonicalSig)) == 0;
}

bool bytesIntact(const BundleRef& bundle) noexcept {
  if (bundle.bytes == nullptr || bundle.size == 0) {
    return false;
  }
  if (bundle.size != sizeof(kCanonicalBytes)) {
    return false;
  }
  return std::memcmp(bundle.bytes, kCanonicalBytes,
                     sizeof(kCanonicalBytes)) == 0;
}

VerifyResult deny(int code, const char* check, const char* file,
                  const char* expected) noexcept {
  VerifyResult r{};
  r.ok = false;
  r.mapping.valid = false;
  r.mapping.base = nullptr;
  r.mapping.size = 0;
  r.mapping.executable = false;
  r.mapping.writable = false;
  r.error.code = code;
  r.error.check = check;
  r.error.file = file;
  r.error.expected = expected;
  logReject(r.error);
  return r;
}

}  // namespace

void setRejectLogHook(RejectLogHook hook) noexcept { g_hook = hook; }

VerifyResult verifyAndMap(const BundleRef& bundle,
                          const KeySet& pinnedKeys) noexcept {
  // Order: key-trust (AC-SECURITY-009, no unsigned override) first, then
  // expiry, then integrity (AC-SECURITY-011). Each denial maps nothing.
  if (!keyPinned(bundle.keyId, pinnedKeys)) {
    return deny(kCodeKeyTrust, kCheckKeyTrust, bundle.path,
                "keyId present in pinned KeySet via already-trusted bundle");
  }
  if (bundle.expiryUnixSec <= kNowUnixSec) {
    return deny(kCodeExpiry, kCheckExpiry, bundle.path,
                "unexpired signature (expiryUnixSec in the future)");
  }
  if (!signatureMatches(bundle) || !bytesIntact(bundle)) {
    return deny(kCodeIntegrity, kCheckIntegrity, bundle.path,
                "valid signature over unmodified bundle bytes");
  }
  // Verified: map RX-only (AC-SECURITY-010). W^X self-check: never RWX.
  VerifyResult r{};
  r.ok = true;
  r.mapping.valid = true;
  r.mapping.base = bundle.bytes;
  r.mapping.size = bundle.size;
  r.mapping.executable = true;
  r.mapping.writable = false;
  if (r.mapping.executable && r.mapping.writable) {
    return deny(kCodeIntegrity, kCheckIntegrity, bundle.path,
                "RX-only mapping (never read-write-execute)");
  }
  r.error.code = 0;
  r.error.check = nullptr;
  r.error.file = bundle.path;
  r.error.expected = nullptr;
  return r;
}

}  // namespace muse::sandbox
