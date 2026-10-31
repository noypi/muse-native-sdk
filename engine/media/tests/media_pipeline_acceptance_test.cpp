// Acceptance test — NFR-MEDIA-002 / FR-MEDIA-010 protected media pipeline.
// Exercises the T1->T2->T4 protected path end to end plus the T3->T4
// offline-relaunch path, and the fail-closed denials for each item.
// Hermetic: C++17 + engine/media headers/sources only; no network,
// no env/config reads, no filesystem, no live services.
// Run from checkout root: compile the four media sources with this file
// with g++ -std=c++17 -I . and execute the resulting binary.
#include <cstdint>
#include <cstdio>

#include "engine/media/include/media/download_resume.h"
#include "engine/media/include/media/drm_session.h"
#include "engine/media/include/media/playback_pipeline.h"
#include "engine/media/include/media/zero_copy_frame.h"

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

// A1: protected streaming path — capable device, live DRM session on a
// trusted path, zero-copy route available -> playing on protected path.
static void check_protected_streaming_path() {
  media::DeviceCaps caps{true, true};
  CHECK("A1 zero-copy available on capable device",
        media::IsZeroCopyAvailable(caps));
  media::HardwareBufferHandle buf{true, 9u};
  bool avoided = false;
  media::ZeroCopyImportResult imp =
      media::ImportHardwareBuffer(buf, /*fence_fd=*/3u, &avoided);
  CHECK("A1 frame import ok", imp.ok && avoided && imp.fence_ok);
  media::KeyHandle key{false, 1u};
  media::LicenseStore store{true, true};
  media::DrmSessionResult sess = media::OpenSession(key, store);
  CHECK("A1 drm session opens", sess.ok && sess.session_id != 0u);
  media::DrmSessionRef drm{true, true};
  media::ZeroCopyImporter importer{true};
  media::PlaybackResult pb =
      media::OpenStream(media::StreamKind::kDash, drm, importer);
  CHECK("A1 dash plays on protected path",
        pb.playing && pb.protected_path &&
            pb.error == media::PlaybackError::kNone);
  media::FrameReleaseResult rel =
      media::ReleaseDecryptedFrame(sess.session_id, media::SecureFrame{77u});
  CHECK("A1 decrypted frame released to session", rel.released);
}

// A2: fail-closed — missing fence means no zero-copy claim and the
// playback gate stays shut (no cleartext fallback).
static void check_fail_closed_no_fence() {
  media::HardwareBufferHandle buf{true, 5u};
  bool avoided = true;
  media::ZeroCopyImportResult imp =
      media::ImportHardwareBuffer(buf, /*fence_fd=*/0u, &avoided);
  CHECK("A2 fenceless import denied", !imp.ok && !avoided);
  CHECK("A2 fenceless denial names a reason", imp.reason != nullptr);
  media::PlaybackResult pb = media::OpenStream(
      media::StreamKind::kHls, media::DrmSessionRef{true, true},
      media::ZeroCopyImporter{false});
  CHECK("A2 stream without zero-copy route denied",
        !pb.playing &&
            pb.error == media::PlaybackError::kUntrustedPathClosed &&
            !pb.retryable);
}

// A3: fail-closed — exposed key and untrusted path never open a session;
// unknown stream kind is rejected as unsupported.
static void check_fail_closed_drm_and_kind() {
  media::LicenseStore good{true, true};
  media::DrmSessionResult exposed =
      media::OpenSession(media::KeyHandle{true, 2u}, good);
  CHECK("A3 exposed key denied",
        !exposed.ok && exposed.error == media::DrmError::kKeyExposed &&
            !exposed.retryable);
  media::LicenseStore bad_path{false, true};
  media::DrmSessionResult untrusted =
      media::OpenSession(media::KeyHandle{false, 3u}, bad_path);
  CHECK("A3 untrusted path denied",
        !untrusted.ok &&
            untrusted.error == media::DrmError::kUntrustedPath);
  media::PlaybackResult pb = media::OpenStream(
      static_cast<media::StreamKind>(999), media::DrmSessionRef{true, true},
      media::ZeroCopyImporter{true});
  CHECK("A3 unknown kind unsupported",
        !pb.playing &&
            pb.error == media::PlaybackError::kUnsupportedStream);
}

// A4: offline relaunch — snapshot survives a re-created downloader and
// the persisted title (bytes + license) plays twice; a licenseless
// title is denied.
static void check_offline_relaunch() {
  media::DownloadState live{1000u, 400u};
  media::DownloadRecord snap = media::Snapshot(live);
  CHECK("A4 snapshot preserves offsets",
        snap.total_bytes == 1000u && snap.done_bytes == 400u);
  // Simulate process death: fresh cursor + fresh destination, same record.
  media::ChunkCursor fresh_cursor{0u};
  media::NetDestination dest{true, 1u << 20};
  media::ResumeResult r = media::ResumeDownload(snap, fresh_cursor, dest);
  CHECK("A4 post-relaunch resumes from snapshot",
        r.ok && r.resumed_from_byte == 400u && !r.complete);
  media::OfflineTitle title{true, true};
  media::PlaybackResult first = media::PlayOffline(title);
  media::PlaybackResult second =
      media::PlayOffline(media::OfflineTitle{true, true});
  CHECK("A4 offline title plays across relaunch",
        first.playing && first.protected_path && second.playing &&
            second.protected_path);
  media::PlaybackResult no_license =
      media::PlayOffline(media::OfflineTitle{true, false});
  CHECK("A4 licenseless offline denied",
        !no_license.playing &&
            no_license.error == media::PlaybackError::kUntrustedPathClosed);
}

// A5: edge cases — corrupt record is unrecoverable (never complete),
// stalled destination is retryable with backoff, release to a dead
// session fails shut.
static void check_edge_cases() {
  media::NetDestination dest{true, 1u << 20};
  media::ResumeResult corrupt = media::ResumeDownload(
      media::DownloadRecord{100u, 101u}, media::ChunkCursor{0u}, dest);
  CHECK("A5 corrupt record unrecoverable",
        !corrupt.ok && !corrupt.complete &&
            corrupt.error == media::ResumeError::kUnrecoverable);
  media::NetDestination stalled{false, 1u << 20};
  media::ResumeResult retry = media::ResumeDownload(
      media::DownloadRecord{100u, 10u}, media::ChunkCursor{0u}, stalled);
  CHECK("A5 stalled destination retryable with backoff",
        !retry.ok && retry.error == media::ResumeError::kRetryable &&
            retry.retry_after_ms > 0u);
  media::FrameReleaseResult dead =
      media::ReleaseDecryptedFrame(0u, media::SecureFrame{1u});
  CHECK("A5 release to dead session denied", !dead.released);
  bool avoided = true;
  media::ZeroCopyImportResult bad = media::ImportHardwareBuffer(
      media::HardwareBufferHandle{false, 0u}, /*fence_fd=*/4u, &avoided);
  CHECK("A5 invalid buffer denied without host-copy claim",
        !bad.ok && !avoided);
  CHECK("A5 nullptr out-param tolerated",
        media::ImportHardwareBuffer(media::HardwareBufferHandle{true, 8u},
                                    /*fence_fd=*/2u, nullptr)
                .ok);
}

int main() {
  check_protected_streaming_path();
  check_fail_closed_no_fence();
  check_fail_closed_drm_and_kind();
  check_offline_relaunch();
  check_edge_cases();
  if (s_failures == 0) {
    std::printf("ALL GREEN media_pipeline_acceptance_test\n");
    return 0;
  }
  std::printf("%d FAILURES media_pipeline_acceptance_test\n", s_failures);
  return 1;
}
