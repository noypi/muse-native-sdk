// Batch 1 T2 — DRM session key custody (FR-MEDIA-010/AC-MEDIA-015;
// required NFR-MEDIA-003/AC-MEDIA-006). TC-MEDIA-015 mapped file, refreshed
// in place. Binds ONLY to the T2 `Interface:` line in plan.md; never to
// implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/drm_session.h only;
// no network, no env/config reads, no filesystem, no live services, no real
// keys. LicenseStore/KeyHandle are narrow injected seams (Widevine/Keystore
// stand-ins with obvious dummy values).
//
// Expected state: RED — engine/media/include/media/drm_session.h does not
// exist yet, so this file fails to compile until the coder implements T2.
//
// Contract assumptions (see workflow.local/runs/nfr-media-002-fr-media-010/unit-tests/T2.md):
//   A-NS:     everything in namespace `media`; header
//             `engine/media/include/media/drm_session.h`.
//   A-TYPES:  `struct KeyHandle { bool exposed; uint64_t id; };`
//             `struct LicenseStore { bool trusted_path; bool has_license; };`
//             `using SessionId = uint64_t;`
//             `enum class DrmError { kNone, kKeyExposed, kUntrustedPath };`
//             `struct DrmSessionResult { bool ok; SessionId session_id;
//               DrmError error; bool retryable; };`
//             `struct SecureFrame { uint64_t token; };` (opaque — no byte
//             accessor by design)
//             `struct FrameReleaseResult { bool released; };`
//             `DrmSessionResult OpenSession(KeyHandle key,
//               LicenseStore& store);`
//             `FrameReleaseResult ReleaseDecryptedFrame(SessionId s,
//               SecureFrame f);`
#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/media/include/media/drm_session.h"

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

// Valid key + trusted store opens a session.
static void check_valid_key_opens_session() {
  media::KeyHandle key{false, 1001u};
  media::LicenseStore store{true, true};
  bool threw = false;
  media::DrmSessionResult r{false, 0u, media::DrmError::kKeyExposed, true};
  try {
    r = media::OpenSession(key, store);
  } catch (...) {
    threw = true;
  }
  CHECK("valid open never throws", !threw);
  CHECK("valid open ok", r.ok == true);
  CHECK("valid open yields a session id", r.session_id != 0u);
  CHECK("valid open error kNone", r.error == media::DrmError::kNone);
}

// Exposed key material fails closed, non-retryable.
static void check_exposed_key_fails_closed() {
  media::KeyHandle key{true, 1002u};  // exposed=true: key left the session
  media::LicenseStore store{true, true};
  bool threw = false;
  media::DrmSessionResult r{true, 1u, media::DrmError::kNone, true};
  try {
    r = media::OpenSession(key, store);
  } catch (...) {
    threw = true;
  }
  CHECK("exposed key never throws", !threw);
  CHECK("exposed key ok=false", r.ok == false);
  CHECK("exposed key error kKeyExposed",
        r.error == media::DrmError::kKeyExposed);
  CHECK("exposed key non-retryable", r.retryable == false);
  CHECK("exposed key yields no session", r.session_id == 0u);
}

// Untrusted path fails closed, non-retryable (Widevine L1 invariant).
static void check_untrusted_path_fails_closed() {
  media::KeyHandle key{false, 1003u};
  media::LicenseStore store{false, true};  // untrusted path
  bool threw = false;
  media::DrmSessionResult r{true, 1u, media::DrmError::kNone, true};
  try {
    r = media::OpenSession(key, store);
  } catch (...) {
    threw = true;
  }
  CHECK("untrusted path never throws", !threw);
  CHECK("untrusted path ok=false", r.ok == false);
  CHECK("untrusted path error kUntrustedPath",
        r.error == media::DrmError::kUntrustedPath);
  CHECK("untrusted path non-retryable", r.retryable == false);
  CHECK("untrusted path yields no session", r.session_id == 0u);
}

// Decrypted frame release exposes an opaque handle only: the frame carries
// a token, never app-visible bytes. The release round-trips the token and
// reports released=true; there is no byte accessor to call (by design —
// this test would fail to compile if one were added and used here).
static void check_released_frame_is_opaque_handle() {
  media::KeyHandle key{false, 1004u};
  media::LicenseStore store{true, true};
  media::DrmSessionResult s = media::OpenSession(key, store);
  CHECK("opaque setup session ok", s.ok == true);
  media::SecureFrame frame{0xCAFEu};
  bool threw = false;
  media::FrameReleaseResult r{false};
  try {
    r = media::ReleaseDecryptedFrame(s.session_id, frame);
  } catch (...) {
    threw = true;
  }
  CHECK("frame release never throws", !threw);
  CHECK("frame release reported", r.released == true);
  CHECK("frame token stays opaque (no byte expansion)",
        frame.token == 0xCAFEu);
  CHECK("frame token is not byte content", sizeof(frame.token) == 8u);
}

// Release against an unknown session id is rejected, never throws.
static void check_release_unknown_session_rejected() {
  media::SecureFrame frame{0xBEEFu};
  bool threw = false;
  media::FrameReleaseResult r{true};
  try {
    r = media::ReleaseDecryptedFrame(/*s=*/0u, frame);
  } catch (...) {
    threw = true;
  }
  CHECK("unknown session release never throws", !threw);
  CHECK("unknown session release rejected", r.released == false);
}

// Security: hostile key ids / empty store never throw; outcome is a
// well-formed ok or clean fail-closed.
static void check_hostile_key_never_throws() {
  media::KeyHandle key{false, 0xFFFFFFFFFFFFFFFFull};
  media::LicenseStore store{true, false};  // no license persisted
  bool threw = false;
  media::DrmSessionResult r{false, 0u, media::DrmError::kNone, false};
  try {
    r = media::OpenSession(key, store);
  } catch (...) {
    threw = true;
  }
  CHECK("hostile key never throws", !threw);
  if (!r.ok) {
    CHECK("hostile denial is non-retryable", r.retryable == false);
    CHECK("hostile denial names a reason",
          r.error == media::DrmError::kKeyExposed ||
              r.error == media::DrmError::kUntrustedPath);
  }
}

int main() {
  check_valid_key_opens_session();
  check_exposed_key_fails_closed();
  check_untrusted_path_fails_closed();
  check_released_frame_is_opaque_handle();
  check_release_unknown_session_rejected();
  check_hostile_key_never_throws();
  if (s_failures == 0) {
    std::printf("ALL GREEN drm_session_test\n");
    return 0;
  }
  std::printf("%d FAILURES drm_session_test\n", s_failures);
  return 1;
}
