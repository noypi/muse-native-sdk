#pragma once

// Core-side sandbox primitive (NFR-CORE-008 / NFR-SECURITY-008).
// Verify-before-execute gate + W^X enforcement over already-pinned keys.
//
// Trust model (plan A4): the caller passes an already-pinned KeySet owned
// by the security/net key facilities. This header never stores keys and
// never reads configuration, the filesystem, the network, or secrets.
// Expiry is carried inline in BundleRef (no OS clock read). No real
// cryptography: the signature/integrity check below is a structural
// stand-in for the test's inline dummy fixtures (signature bytes compared
// against the pinned entry, canonical payload comparison for the
// tampered-byte marker, inline expiry timestamp). A real backend swaps
// the structural check for crypto without changing this surface.
//
// Loader trust is EXTERNAL (NFR-CORE-003): plugin .so paths are accepted
// only post-verification by the caller; this unit never touches the
// loader. Every denial is fail-closed (FR-PKG-005): ok=false with
// SandboxError{ code, check, file, expected } and nothing mapped.

#include <cstddef>
#include <cstdint>

namespace muse::sandbox {

// Reference to an OTA bundle awaiting verification. Aggregate.
struct BundleRef {
  const char* path = nullptr;
  const uint8_t* bytes = nullptr;
  std::size_t size = 0;
  const uint8_t* signature = nullptr;
  std::size_t signatureSize = 0;
  std::uint64_t expiryUnixSec = 0;
  const char* keyId = nullptr;
};

// Already-pinned key set (input only, never stored). Aggregate over
// NUL-terminated key ids compared with strcmp.
struct KeySet {
  const char* const* keyIds = nullptr;
  std::size_t count = 0;
};

// Executable page mapping descriptor. Success is always RX-only:
// executable=true, writable=false (never RWX). Aggregate.
struct Mapping {
  bool valid = false;
  const void* base = nullptr;
  std::size_t size = 0;
  bool executable = false;
  bool writable = false;
};

// Fail-closed denial shape (FR-PKG-005). Aggregate. check is one of
// "signature-integrity", "signature-expiry", "key-trust".
struct SandboxError {
  int code = 0;
  const char* check = nullptr;
  const char* file = nullptr;
  const char* expected = nullptr;
};

// Result of verifyAndMap. Denial: ok=false, mapping invalid,
// error.code != 0. Success: ok=true, RX-only mapping,
// error.code == 0. Aggregate.
struct VerifyResult {
  bool ok = false;
  Mapping mapping{};
  SandboxError error{};
};

// Logging hook for AC-SECURITY-011 rejection logging. Called once per
// denial with the denial error. Defaults to no-op; never throws.
// A host may install a sink via setRejectLogHook. Install-before-use:
// the hook must be installed single-threaded before any verifyAndMap
// call that may run concurrently; reinstalling while verification is in
// flight is not supported. The pointer itself is stored atomically so a
// late install can never cause a data race (undefined behavior).
using RejectLogHook = void (*)(const SandboxError&) noexcept;
void setRejectLogHook(RejectLogHook hook) noexcept;

// Verify-before-execute gate. Returns ok(mapping) only after signature
// verification against pinnedKeys with executable pages mapped
// read-execute (never read-write-execute); every denial returns
// err(SandboxError{...}) and maps/loads nothing. Never throws.
VerifyResult verifyAndMap(const BundleRef& bundle,
                          const KeySet& pinnedKeys) noexcept;

}  // namespace muse::sandbox
