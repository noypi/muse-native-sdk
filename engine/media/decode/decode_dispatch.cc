#include "engine/media/decode/decode_dispatch.h"

#include <climits>

#include "engine/graphics/textures/texture_pool.h"

namespace media {

MediaDecodeOutcome dispatchDecode(core::thread::WorkerPool& pool,
                                 DecodedImage* out, const uint8_t* bytes,
                                 std::size_t len, const DecodeRequest& req) {
  // Synchronous InvalidArg validation: never touches *out, never enqueues.
  if (bytes == nullptr || len == 0 || out == nullptr) {
    return MediaDecodeOutcome{false, Status::kInvalidArg};
  }
  if (req.budget == nullptr) {
    return MediaDecodeOutcome{false, Status::kInvalidArg};
  }
  // Synchronous VRAM admission on the caller thread: out untouched on reject.
  if (!req.budget->acquireVram(req.vramBytes)) {
    return MediaDecodeOutcome{false, Status::kOverBudget};
  }

  // Copy the token: copies share state, so a cancel() on the caller's copy
  // is observable through the copy captured by the worker body (cooperative
  // cancellation per core::thread::task.h).
  core::thread::CancellationToken tok = req.tok;
  core::budget::Manager* budget = req.budget;
  const std::size_t vramBytes = req.vramBytes;

  core::thread::Task task;
  task.pri = core::thread::Priority::Decode;
  task.tok = tok;
  task.fn = [tok, budget, vramBytes, out] {
    // Cooperative cancellation: skip the upload entirely on cancel.
    // The reservation is returned so a queued-then-cancelled decode leaks
    // neither VRAM accounting nor the worker slot (clean shutdown join).
    if (tok.isCancelled()) {
      budget->releaseVram(vramBytes);
      return;
    }
    // Decode body: applyMetadata placeholder (Batch 2 T3/T4 EXIF, color
    // profile, and target-size correction lands here; identity until then —
    // no codec linkage per plan A3, never throws on malformed bytes).
    // Upload via poolAcquire accounting (mirrors texture_pool.h admit path).
    int uploadBytes =
        vramBytes > static_cast<std::size_t>(INT_MAX)
            ? INT_MAX
            : static_cast<int>(vramBytes);
    if (uploadBytes <= 0) {
      uploadBytes = 1;
    }
    PoolState state;
    state.capacityBytes = uploadBytes;
    state.usedBytes = 0;
    state.ringSlots = 64;
    state.usedSlots = 0;
    TextureRequest treq;
    treq.bytes = uploadBytes;
    const PoolOutcome oc = poolAcquire(state, treq);
    if (!oc.admitted) {
      budget->releaseVram(vramBytes);
      return;
    }
    // Only observable write: success flips uploaded after poolAcquire admits.
    out->uploaded = true;
  };
  pool.submit(std::move(task));
  return MediaDecodeOutcome{true, Status::kOk};
}

}  // namespace media
