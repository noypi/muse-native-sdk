#include "engine/media/include/media/playback_pipeline.h"

namespace media {

namespace {

// Fail-closed denial helper: well-formed structured denial, non-retryable.
PlaybackResult Deny(PlaybackError error) noexcept {
  PlaybackResult r;
  r.playing = false;
  r.protected_path = false;
  r.error = error;
  r.retryable = false;
  return r;
}

bool IsKnownKind(StreamKind kind) noexcept {
  return kind == StreamKind::kHls || kind == StreamKind::kDash ||
         kind == StreamKind::kSmooth;
}

}  // namespace

PlaybackResult OpenStream(StreamKind kind, const DrmSessionRef& drm,
                          const ZeroCopyImporter& importer) noexcept {
  // Unknown container first: unsupported stream, fail closed. (When the DRM
  // or importer seams are also bad the outcome stays a well-formed denial;
  // the kind error takes precedence as the most specific cause.)
  if (!IsKnownKind(kind)) return Deny(PlaybackError::kUnsupportedStream);
  // Widevine L1 invariant: protected content requires a live session on a
  // trusted path. Missing session gets no cleartext fallback — fail closed.
  if (!drm.has_session || !drm.trusted_path) {
    return Deny(PlaybackError::kUntrustedPathClosed);
  }
  // RULE-MEDIA-003 routing gate: decoded frames must flow through the T1
  // zero-copy importer. Unavailable route fails closed, never cleartext.
  if (!importer.available) return Deny(PlaybackError::kUntrustedPathClosed);
  // No PiP side effect here: LIFE-PIP entry stays an external callback.
  PlaybackResult r;
  r.playing = true;
  r.protected_path = true;
  r.error = PlaybackError::kNone;
  r.retryable = false;
  return r;
}

PlaybackResult PlayOffline(const OfflineTitle& t) noexcept {
  // License required: bytes without a persisted license are denied.
  if (!t.has_bytes || !t.has_license) {
    return Deny(PlaybackError::kUntrustedPathClosed);
  }
  // No PiP side effect here: LIFE-PIP entry stays an external callback.
  PlaybackResult r;
  r.playing = true;
  r.protected_path = true;
  r.error = PlaybackError::kNone;
  r.retryable = false;
  return r;
}

}  // namespace media
