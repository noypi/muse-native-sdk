#pragma once

#include <cstddef>
#include <cstdint>

#include "core/budget/manager.h"
#include "core/thread/task.h"
#include "core/thread/worker_pool.h"

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

struct DecodedImage {
  bool uploaded = false;
};

MediaDecodeOutcome dispatchDecode(core::thread::WorkerPool& pool,
                                 DecodedImage* out, const uint8_t* bytes,
                                 std::size_t len, const DecodeRequest& req);

}  // namespace media
