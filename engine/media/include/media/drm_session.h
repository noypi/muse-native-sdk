#pragma once

// T2 — DRM session key custody (FR-MEDIA-010/AC-MEDIA-015;
// required NFR-MEDIA-003/AC-MEDIA-006).
//
// Keys and licenses are held by the platform session object; decrypted
// frames never surface bytes to the app and are never written to disk.
// `SecureFrame` carries an opaque token only — there is intentionally no
// byte accessor.
//
// Hermetic: C++17, no network/env/filesystem/live-service deps. The
// Widevine/Keystore platform session is consumed as narrow injected
// seams (`KeyHandle` / `LicenseStore`) with dummy ids only.

#include <cstdint>

namespace media {

struct KeyHandle {
  bool exposed = false;
  uint64_t id = 0u;
};

struct LicenseStore {
  bool trusted_path = false;
  bool has_license = false;
};

using SessionId = uint64_t;

enum class DrmError { kNone, kKeyExposed, kUntrustedPath };

struct DrmSessionResult {
  bool ok = false;
  SessionId session_id = 0u;
  DrmError error = DrmError::kNone;
  bool retryable = false;
};

// Opaque decrypted-frame handle. `token` round-trips through release;
// it is not byte content and there is no accessor exposing bytes.
struct SecureFrame {
  uint64_t token = 0u;
};

struct FrameReleaseResult {
  bool released = false;
};

// Opens a DRM session holding `key` against `store`.
//  - key.exposed -> {false, 0, kKeyExposed, false} (fail closed)
//  - !store.trusted_path or !store.has_license -> {false, 0, kUntrustedPath,
//    false} (fail closed)
//  - otherwise -> {true, <nonzero id>, kNone, false}
// Never throws.
DrmSessionResult OpenSession(KeyHandle key, LicenseStore& store) noexcept;

// Releases a decrypted frame back to session `s`. The frame stays opaque:
// no bytes are exposed and nothing is written to disk. Returns
// {released=true} when `s` names a live session, {released=false}
// otherwise (including session 0). Never throws.
FrameReleaseResult ReleaseDecryptedFrame(SessionId s, SecureFrame f) noexcept;

}  // namespace media
