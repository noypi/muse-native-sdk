#pragma once

#include <cstddef>
#include <cstdint>

#include "core/budget/manager.h"
#include "core/thread/task.h"
#include "core/thread/worker_pool.h"
#include "engine/media/decode/image_metadata.h"

namespace media {

// T2 shapes pinned by unit-tests/T2.md assumptions (A-NS, A-REQ, A-OUT, A-OK).
// T1 provides these as stubs only; gated-dispatch/completion/upload stays
// unimplemented until T2.
enum class Status { kOk, kOverBudget, kInvalidArg };

struct MediaDecodeOutcome {
  bool accepted = false;
  Status status = Status::kInvalidArg;
};

struct DecodeRequest {
  core::thread::CancellationToken tok;
  core::budget::Manager* budget = nullptr;
  std::size_t vramBytes = 0;
};

// DecodedImage is the shared rich type owned by image_metadata.h
// (Interface ruling); T2 uploaded semantics unchanged.

// Contract (security-review MEDIUM + LOW): dispatchDecode copies `len` bytes
// from `bytes` into an owned task-closure buffer on the caller thread, so
// the caller may release or reuse its `bytes` storage as soon as dispatch
// returns. The caller must not touch `*out` from dispatch until pool
// join/shutdown (not just before reading): the worker reads `*out` as its
// decode seed and writes the upload result, so any concurrent caller access
// races the worker.
MediaDecodeOutcome dispatchDecode(core::thread::WorkerPool& pool,
                                 DecodedImage* out, const uint8_t* bytes,
                                 std::size_t len, const DecodeRequest& req);

}  // namespace media
