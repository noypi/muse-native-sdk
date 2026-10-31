#include "engine/media/include/media/download_resume.h"

namespace media {

namespace {

// Transient NET-STORAGE stall backoff hint (implementation-chosen).
constexpr uint64_t kRetryBackoffMs = 100u;

}  // namespace

ResumeResult ResumeDownload(const DownloadRecord& persisted, ChunkCursor cursor,
                            NetDestination& dest) noexcept {
  // Silence unused-parameter warnings while keeping the injected seam
  // visible; the persisted record is the resume source of truth.
  (void)cursor.offset;
  (void)dest.capacity_bytes;

  // Corrupt persisted state fails closed and is never flagged complete.
  if (persisted.done_bytes > persisted.total_bytes) {
    ResumeResult r;
    r.ok = false;
    r.resumed_from_byte = persisted.done_bytes;
    r.complete = false;
    r.error = ResumeError::kUnrecoverable;
    r.retry_after_ms = 0u;
    return r;
  }

  // Transient destination stall: retryable with a backoff hint.
  if (!dest.writable) {
    ResumeResult r;
    r.ok = false;
    r.resumed_from_byte = persisted.done_bytes;
    r.complete = false;
    r.error = ResumeError::kRetryable;
    r.retry_after_ms = kRetryBackoffMs;
    return r;
  }

  ResumeResult r;
  r.ok = true;
  r.resumed_from_byte = persisted.done_bytes;
  r.complete = persisted.done_bytes == persisted.total_bytes;
  r.error = ResumeError::kNone;
  r.retry_after_ms = 0u;
  return r;
}

DownloadRecord Snapshot(const DownloadState& s) noexcept {
  DownloadRecord record;
  record.total_bytes = s.total_bytes;
  record.done_bytes = s.done_bytes;
  return record;
}

}  // namespace media
