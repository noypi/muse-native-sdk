// Batch 2 T3 — resumable offline download (FR-MEDIA-010/AC-MEDIA-016).
// TC-MEDIA-016 mapped file, created here. Binds ONLY to the T3 `Interface:`
// line in plan.md; never to implementation.
//
// Hermetic: C++17 + the new engine/media/include/media/download_resume.h
// only; no network, no env/config reads, no filesystem, no live services.
// NET-STORAGE (destination handle) and STATE-SYNC (queue record) are narrow
// injected structs with inline dummy values.
//
// Expected state: RED — engine/media/include/media/download_resume.h does not
// exist yet, so this file fails to compile until the coder implements T3.
//
// Contract assumptions (see workflow.local/runs/nfr-media-002-fr-media-010/unit-tests/T3.md):
//   A-NS:     everything in namespace `media`; header
//             `engine/media/include/media/download_resume.h`.
//   A-TYPES:  `enum class ResumeError { kNone, kUnrecoverable, kRetryable };`
//             `struct DownloadRecord { uint64_t total_bytes; uint64_t done_bytes; };`
//             `struct ChunkCursor { uint64_t offset; };`
//             `struct NetDestination { bool writable; uint64_t capacity_bytes; };`
//             `struct DownloadState { uint64_t total_bytes; uint64_t done_bytes; };`
//             `struct ResumeResult { bool ok; uint64_t resumed_from_byte;
//               bool complete; ResumeError error; uint64_t retry_after_ms; };`
//             `ResumeResult ResumeDownload(const DownloadRecord& persisted,
//               ChunkCursor cursor, NetDestination& dest);`
//             `DownloadRecord Snapshot(const DownloadState& s);`
#include <cstdint>
#include <cstdio>
#include <string>

#include "engine/media/include/media/download_resume.h"

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

// Fresh download: empty persisted record resumes from byte 0, not complete.
static void check_fresh_download_starts_at_zero() {
  media::DownloadRecord persisted{1024u, 0u};
  media::ChunkCursor cursor{0u};
  media::NetDestination dest{true, 4096u};
  bool threw = false;
  media::ResumeResult r{false, 0u, false, media::ResumeError::kUnrecoverable,
                        0u};
  try {
    r = media::ResumeDownload(persisted, cursor, dest);
  } catch (...) {
    threw = true;
  }
  CHECK("fresh resume never throws", !threw);
  CHECK("fresh resume ok", r.ok == true);
  CHECK("fresh resume starts at byte 0", r.resumed_from_byte == 0u);
  CHECK("fresh resume not complete", r.complete == false);
  CHECK("fresh resume error kNone", r.error == media::ResumeError::kNone);
}

// Process-death survival: snapshot a partial state, destroy it, re-create
// from the snapshot, and resume from the preserved offset.
static void check_snapshot_round_trip_survives_process_death() {
  media::DownloadState live{2048u, 768u};
  media::DownloadRecord snap{0u, 0u};
  bool threw = false;
  try {
    snap = media::Snapshot(live);
  } catch (...) {
    threw = true;
  }
  CHECK("snapshot never throws", !threw);
  CHECK("snapshot preserves total", snap.total_bytes == 2048u);
  CHECK("snapshot preserves done", snap.done_bytes == 768u);
  // Simulate process death: `live` is gone; only `snap` remains.
  media::ChunkCursor cursor{snap.done_bytes};
  media::NetDestination dest{true, 8192u};
  media::ResumeResult r{false, 0u, false, media::ResumeError::kUnrecoverable,
                        0u};
  try {
    r = media::ResumeDownload(snap, cursor, dest);
  } catch (...) {
    threw = true;
  }
  CHECK("post-relaunch resume never throws", !threw);
  CHECK("post-relaunch resume ok", r.ok == true);
  CHECK("post-relaunch resumes from snapshot offset",
        r.resumed_from_byte == 768u);
  CHECK("partial resume not complete", r.complete == false);
}

// Completion is flagged only when every byte is present.
static void check_complete_only_when_all_bytes_present() {
  media::DownloadRecord full{512u, 512u};
  media::ChunkCursor cursor{512u};
  media::NetDestination dest{true, 4096u};
  media::ResumeResult r = media::ResumeDownload(full, cursor, dest);
  CHECK("full download ok", r.ok == true);
  CHECK("full download complete", r.complete == true);
  CHECK("full download resumes at end", r.resumed_from_byte == 512u);

  media::DownloadRecord partial{512u, 511u};
  media::ChunkCursor cursor2{511u};
  media::ResumeResult r2 = media::ResumeDownload(partial, cursor2, dest);
  CHECK("one-byte-short ok", r2.ok == true);
  CHECK("one-byte-short not complete", r2.complete == false);
}

// Corrupt persisted record (done > total) is unrecoverable, non-retried.
static void check_corrupt_record_unrecoverable() {
  media::DownloadRecord bad{100u, 200u};  // done exceeds total: corrupt
  media::ChunkCursor cursor{200u};
  media::NetDestination dest{true, 4096u};
  bool threw = false;
  media::ResumeResult r{true, 0u, false, media::ResumeError::kNone, 0u};
  try {
    r = media::ResumeDownload(bad, cursor, dest);
  } catch (...) {
    threw = true;
  }
  CHECK("corrupt record never throws", !threw);
  CHECK("corrupt record ok=false", r.ok == false);
  CHECK("corrupt record error kUnrecoverable",
        r.error == media::ResumeError::kUnrecoverable);
  CHECK("corrupt record not complete", r.complete == false);
}

// Unwritable destination is a retryable failure with a backoff hint.
static void check_unwritable_destination_retryable() {
  media::DownloadRecord persisted{1024u, 256u};
  media::ChunkCursor cursor{256u};
  media::NetDestination dest{false, 4096u};  // NET-STORAGE seam: no capacity
  bool threw = false;
  media::ResumeResult r{true, 0u, false, media::ResumeError::kNone, 0u};
  try {
    r = media::ResumeDownload(persisted, cursor, dest);
  } catch (...) {
    threw = true;
  }
  CHECK("unwritable dest never throws", !threw);
  CHECK("unwritable dest ok=false", r.ok == false);
  CHECK("unwritable dest error kRetryable",
        r.error == media::ResumeError::kRetryable);
  CHECK("retryable carries a backoff", r.retry_after_ms > 0u);
}

// Security: hostile cursor/record values never throw; outcome is a
// well-formed ok or a clean structured error.
static void check_hostile_values_never_throw() {
  media::DownloadRecord persisted{0xFFFFFFFFFFFFFFFFull,
                                  0xFFFFFFFFFFFFFFFFull};
  media::ChunkCursor cursor{0xFFFFFFFFFFFFFFFFull};
  media::NetDestination dest{true, 0u};
  bool threw = false;
  media::ResumeResult r{false, 0u, false, media::ResumeError::kNone, 0u};
  try {
    r = media::ResumeDownload(persisted, cursor, dest);
  } catch (...) {
    threw = true;
  }
  CHECK("hostile values never throw", !threw);
  if (!r.ok) {
    CHECK("hostile denial names a reason",
          r.error == media::ResumeError::kUnrecoverable ||
              r.error == media::ResumeError::kRetryable);
  }
}

int main() {
  check_fresh_download_starts_at_zero();
  check_snapshot_round_trip_survives_process_death();
  check_complete_only_when_all_bytes_present();
  check_corrupt_record_unrecoverable();
  check_unwritable_destination_retryable();
  check_hostile_values_never_throw();
  if (s_failures == 0) {
    std::printf("ALL GREEN download_resume_test\n");
    return 0;
  }
  std::printf("%d FAILURES download_resume_test\n", s_failures);
  return 1;
}
