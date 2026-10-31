// T2 implementation — see engine/media/include/media/drm_session.h.
#include "engine/media/include/media/drm_session.h"

#include <atomic>
#include <mutex>
#include <unordered_set>

namespace media {
namespace {

// Live session ids issued by OpenSession in this process. Keys/licenses
// stay inside the session object; only the id leaves it.
std::mutex& SessionMutex() {
  static std::mutex m;
  return m;
}

std::unordered_set<SessionId>& LiveSessions() {
  static std::unordered_set<SessionId> live;
  return live;
}

std::atomic<uint64_t>& NextSessionId() {
  static std::atomic<uint64_t> next{1u};
  return next;
}

bool IsLive(SessionId s) noexcept {
  if (s == 0u) return false;
  try {
    std::lock_guard<std::mutex> lock(SessionMutex());
    return LiveSessions().find(s) != LiveSessions().end();
  } catch (...) {
    return false;
  }
}

}  // namespace

DrmSessionResult OpenSession(KeyHandle key, LicenseStore& store) noexcept {
  try {
    // Key material must never leave the platform session.
    if (key.exposed) {
      return DrmSessionResult{false, 0u, DrmError::kKeyExposed, false};
    }
    // Protected path + persisted license required (Widevine L1 invariant).
    // Missing license fails closed on the same untrusted-path denial so
    // callers cannot distinguish "no license" from "untrusted output".
    if (!store.trusted_path || !store.has_license) {
      return DrmSessionResult{false, 0u, DrmError::kUntrustedPath, false};
    }
    (void)key.id;
    (void)store;
    uint64_t id = NextSessionId().fetch_add(1u);
    if (id == 0u) id = NextSessionId().fetch_add(1u);
    {
      std::lock_guard<std::mutex> lock(SessionMutex());
      LiveSessions().insert(id);
    }
    return DrmSessionResult{true, id, DrmError::kNone, false};
  } catch (...) {
    return DrmSessionResult{false, 0u, DrmError::kUntrustedPath, false};
  }
}

FrameReleaseResult ReleaseDecryptedFrame(SessionId s, SecureFrame f) noexcept {
  try {
    // Opaque handle only: the token is never expanded into bytes and
    // nothing is written to disk (NFR-MEDIA-003/AC-MEDIA-006).
    (void)f.token;
    if (!IsLive(s)) return FrameReleaseResult{false};
    return FrameReleaseResult{true};
  } catch (...) {
    return FrameReleaseResult{false};
  }
}

}  // namespace media
