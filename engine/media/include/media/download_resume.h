#pragma once

// T3 — resumable offline download (FR-MEDIA-010/AC-MEDIA-016).
//
// NET-STORAGE (destination handle) and STATE-SYNC (queue record) are
// consumed as narrow injected structs only (`NetDestination` /
// `DownloadRecord` / `ChunkCursor`) with dummy byte counts; no network,
// filesystem, or live-service deps.
//
// Hermetic: C++17, no network/env/filesystem/live-service deps.

#include <cstdint>

namespace media {

enum class ResumeError { kNone, kUnrecoverable, kRetryable };

struct DownloadRecord {
  uint64_t total_bytes = 0u;
  uint64_t done_bytes = 0u;
};

struct ChunkCursor {
  uint64_t offset = 0u;
};

struct NetDestination {
  bool writable = false;
  uint64_t capacity_bytes = 0u;
};

struct DownloadState {
  uint64_t total_bytes = 0u;
  uint64_t done_bytes = 0u;
};

struct ResumeResult {
  bool ok = false;
  uint64_t resumed_from_byte = 0u;
  bool complete = false;
  ResumeError error = ResumeError::kNone;
  uint64_t retry_after_ms = 0u;
};

// Resumes a download from the persisted STATE-SYNC record.
//  - done_bytes > total_bytes -> {false, done, false, kUnrecoverable, 0}
//    (corrupt record, fail closed)
//  - !dest.writable -> {false, done, false, kRetryable, <backoff>}
//    (transient NET-STORAGE stall)
//  - otherwise -> {true, done, done == total, kNone, 0}; a re-created
//    instance resumes from the snapshot offset (process-death survival).
// Never throws.
ResumeResult ResumeDownload(const DownloadRecord& persisted, ChunkCursor cursor,
                            NetDestination& dest) noexcept;

// Persists the live cursor so a re-created instance resumes from the
// snapshot. Copies total/done verbatim. Never throws.
DownloadRecord Snapshot(const DownloadState& s) noexcept;

}  // namespace media
