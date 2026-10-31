#pragma once

// T4 — playback pipeline (FR-MEDIA-010/AC-MEDIA-017; RULE-MEDIA-003).
//
// Media3-style adaptive-stream playback: HLS/DASH/SmoothStreaming kinds open
// on a trusted protected path with decoded frames routed through the T1
// zero-copy importer and the T2 DRM session concepts (consumed here as
// narrow injected seams `ZeroCopyImporter` / `DrmSessionRef` with dummy
// flags; no reimplementation, no keystore code).
//
// LIFE-PIP is a PiP-entry callback only: OpenStream/PlayOffline perform no
// PiP side effect; real PiP lifecycle stays external.
//
// Hermetic: C++17, no network/env/filesystem/live-service deps. Never throws.

namespace media {

enum class StreamKind { kHls, kDash, kSmooth };

// T2 session seam (Widevine L1 stand-in): has_session names a live platform
// session; trusted_path names the protected-path invariant.
struct DrmSessionRef {
  bool has_session = false;
  bool trusted_path = false;
};

// T1 importer seam (RULE-MEDIA-003 routing gate): available names a bound
// AHardwareBuffer zero-copy output.
struct ZeroCopyImporter {
  bool available = false;
};

enum class PlaybackError { kNone, kUnsupportedStream, kUntrustedPathClosed };

struct PlaybackResult {
  bool playing = false;
  bool protected_path = false;
  PlaybackError error = PlaybackError::kNone;
  bool retryable = false;
};

// Offline title seam (T3 license-persistence stand-in): has_bytes names
// downloaded bytes; has_license names the persisted license. Relaunch is a
// re-constructed identical value (no live state).
struct OfflineTitle {
  bool has_bytes = false;
  bool has_license = false;
};

// Opens an adaptive stream.
//  - unknown kind -> {false, false, kUnsupportedStream, false}
//  - !drm.has_session or !drm.trusted_path (Widevine L1 invariant,
//    no cleartext fallback) -> {false, false, kUntrustedPathClosed, false}
//  - !importer.available (zero-copy route unavailable) ->
//    {false, false, kUntrustedPathClosed, false}
//  - otherwise -> {true, true, kNone, false}
// Never throws.
PlaybackResult OpenStream(StreamKind kind, const DrmSessionRef& drm,
                          const ZeroCopyImporter& importer) noexcept;

// Plays a downloaded title offline.
//  - has_bytes && has_license -> {true, true, kNone, false} (plays across
//    relaunch: the same persisted value plays twice)
//  - otherwise (missing bytes or missing license) ->
//    {false, false, kUntrustedPathClosed, false} (fail closed)
// Never throws.
PlaybackResult PlayOffline(const OfflineTitle& t) noexcept;

}  // namespace media
