// Batch 2 T4 — playback pipeline (FR-MEDIA-010/AC-MEDIA-017; RULE-MEDIA-003).
// New hermetic contract (TC-MEDIA-017 device-lab stays an external evidence
// note). Binds ONLY to the T4 `Interface:` line in plan.md; never to
// implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/playback_pipeline.h
// only; no network, no env/config reads, no filesystem, no live services.
// DrmSessionRef / ZeroCopyImporter / OfflineTitle are narrow injected seams
// (T2 session, T1 importer, license-persistence stand-ins with dummy values).
// LIFE-PIP is a PiP-entry callback only — asserted hermetically as "no PiP
// side effect inside OpenStream/PlayOffline"; real PiP stays external.
//
// Expected state: RED — engine/media/include/media/playback_pipeline.h does
// not exist yet, so this file fails to compile until the coder implements T4.
//
// Contract assumptions (see workflow.local/runs/nfr-media-002-fr-media-010/unit-tests/T4.md):
//   A-NS:     everything in namespace `media`; header
//             `engine/media/include/media/playback_pipeline.h`.
//   A-TYPES:  `enum class StreamKind { kHls, kDash, kSmooth };`
//             `struct DrmSessionRef { bool has_session; bool trusted_path; };`
//             `struct ZeroCopyImporter { bool available; };`
//             `enum class PlaybackError { kNone, kUnsupportedStream,
//               kUntrustedPathClosed };`
//             `struct PlaybackResult { bool playing; bool protected_path;
//               PlaybackError error; bool retryable; };`
//             `struct OfflineTitle { bool has_bytes; bool has_license; };`
//             `PlaybackResult OpenStream(StreamKind kind,
//               const DrmSessionRef& drm, const ZeroCopyImporter& importer);`
//             `PlaybackResult PlayOffline(const OfflineTitle& t);`
// Built and run via the T3-owned run-media.sh Batch 2 extension; T4 does not
// edit run-media.sh.
#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/media/include/media/playback_pipeline.h"

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

// Each adaptive-stream kind opens on a trusted protected path.
static void check_each_stream_kind_opens() {
  const media::StreamKind kinds[] = {media::StreamKind::kHls,
                                     media::StreamKind::kDash,
                                     media::StreamKind::kSmooth};
  for (int i = 0; i < 3; ++i) {
    media::DrmSessionRef drm{true, true};
    media::ZeroCopyImporter importer{true};
    bool threw = false;
    media::PlaybackResult r{false, false, media::PlaybackError::kNone, false};
    try {
      r = media::OpenStream(kinds[i], drm, importer);
    } catch (...) {
      threw = true;
    }
    CHECK("open kind never throws", !threw);
    CHECK("open kind playing", r.playing == true);
    CHECK("open kind protected path", r.protected_path == true);
    CHECK("open kind error kNone", r.error == media::PlaybackError::kNone);
  }
}

// Widevine L1 on an untrusted path fails closed, non-retryable.
static void check_untrusted_path_fails_closed() {
  media::DrmSessionRef drm{true, false};  // session exists, path untrusted
  media::ZeroCopyImporter importer{true};
  bool threw = false;
  media::PlaybackResult r{true, true, media::PlaybackError::kNone, true};
  try {
    r = media::OpenStream(media::StreamKind::kDash, drm, importer);
  } catch (...) {
    threw = true;
  }
  CHECK("untrusted path never throws", !threw);
  CHECK("untrusted path not playing", r.playing == false);
  CHECK("untrusted path error kUntrustedPathClosed",
        r.error == media::PlaybackError::kUntrustedPathClosed);
  CHECK("untrusted path non-retryable", r.retryable == false);
}

// Missing DRM session fails closed (no cleartext fallback for protected
// content), non-retryable.
static void check_missing_session_fails_closed() {
  media::DrmSessionRef drm{false, true};
  media::ZeroCopyImporter importer{true};
  bool threw = false;
  media::PlaybackResult r{true, true, media::PlaybackError::kNone, true};
  try {
    r = media::OpenStream(media::StreamKind::kHls, drm, importer);
  } catch (...) {
    threw = true;
  }
  CHECK("missing session never throws", !threw);
  CHECK("missing session not playing", r.playing == false);
  CHECK("missing session error kUntrustedPathClosed",
        r.error == media::PlaybackError::kUntrustedPathClosed);
  CHECK("missing session non-retryable", r.retryable == false);
}

// Offline title with bytes + persisted license plays across relaunch:
// the same value plays twice (snapshot round-trip stand-in — no live state).
static void check_offline_title_plays_across_relaunch() {
  media::OfflineTitle t{true, true};
  media::PlaybackResult r1 = media::PlayOffline(t);
  CHECK("offline first play playing", r1.playing == true);
  CHECK("offline first play protected path", r1.protected_path == true);
  // Simulate relaunch: re-construct the identical persisted title value.
  media::OfflineTitle relaunched{true, true};
  bool threw = false;
  media::PlaybackResult r2{false, false, media::PlaybackError::kNone, false};
  try {
    r2 = media::PlayOffline(relaunched);
  } catch (...) {
    threw = true;
  }
  CHECK("offline relaunch never throws", !threw);
  CHECK("offline relaunch playing", r2.playing == true);
  CHECK("offline relaunch protected path", r2.protected_path == true);
}

// Offline title without a persisted license fails closed.
static void check_offline_without_license_fails_closed() {
  media::OfflineTitle t{true, false};  // bytes present, license missing
  bool threw = false;
  media::PlaybackResult r{true, true, media::PlaybackError::kNone, true};
  try {
    r = media::PlayOffline(t);
  } catch (...) {
    threw = true;
  }
  CHECK("licenseless offline never throws", !threw);
  CHECK("licenseless offline not playing", r.playing == false);
  CHECK("licenseless offline error kUntrustedPathClosed",
        r.error == media::PlaybackError::kUntrustedPathClosed);
}

// Security: hostile enum value / empty seams never throw; outcome is a
// well-formed play or a clean structured denial.
static void check_hostile_stream_never_throws() {
  media::DrmSessionRef drm{true, true};
  media::ZeroCopyImporter importer{false};  // zero-copy unavailable
  bool threw = false;
  media::PlaybackResult r{false, false, media::PlaybackError::kNone, false};
  try {
    r = media::OpenStream(static_cast<media::StreamKind>(99), drm, importer);
  } catch (...) {
    threw = true;
  }
  CHECK("hostile stream never throws", !threw);
  if (!r.playing) {
    CHECK("hostile denial names a reason",
          r.error == media::PlaybackError::kUnsupportedStream ||
              r.error == media::PlaybackError::kUntrustedPathClosed);
  }
}

int main() {
  check_each_stream_kind_opens();
  check_untrusted_path_fails_closed();
  check_missing_session_fails_closed();
  check_offline_title_plays_across_relaunch();
  check_offline_without_license_fails_closed();
  check_hostile_stream_never_throws();
  if (s_failures == 0) {
    std::printf("ALL GREEN playback_pipeline_test\n");
    return 0;
  }
  std::printf("%d FAILURES playback_pipeline_test\n", s_failures);
  return 1;
}
