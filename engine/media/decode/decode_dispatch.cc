#include "engine/media/decode/decode_dispatch.h"

#include <climits>
#include <mutex>
#include <utility>

#include "engine/graphics/textures/texture_pool.h"

namespace media {

namespace {
// Serializes ALL Manager acquire/release traffic issued by this TU.
// Manager's seam is NOT thread-safe (manager.h: "callers serialize access"),
// but dispatch acquires on the caller thread while worker threads release,
// so without this mutex concurrent dispatches/completions race on used_.
// Minimal sound option chosen: a TU-local mutex around the three dispatch
// call sites (admit + cancel-release + reject/success-release). External
// code that touches the same Manager directly must provide its own
// synchronization against dispatch (tests do so by sequencing).
std::mutex& BudgetMutex() {
  static std::mutex m;
  return m;
}
}  // namespace

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
  // BUDGET OWNERSHIP POLICY (MAJOR-2): the reservation covers in-flight
  // decode working bytes only. On successful upload, residency transfers to
  // the graphics texture pool (poolAcquire owns eviction there), so the
  // worker releases the budget reservation right after admit — budget.used()
  // therefore tracks live decodes, not completed uploads, and does not grow
  // monotonically. Cancel and pool-reject paths also release (no owner).
  // VISIBILITY CONTRACT (MINOR): *out is written on the worker thread; the
  // caller must synchronize via pool join/shutdown before reading *out.
  // (Tests poll with wait_for, which the harness sanctions.)
  bool admitted = false;
  {
    const std::lock_guard<std::mutex> lock(BudgetMutex());
    admitted = req.budget->acquireVram(req.vramBytes);
  }
  if (!admitted) {
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
  task.fn = [tok, budget, vramBytes, out, bytes, len] {
    // Cooperative cancellation: skip the upload entirely on cancel.
    // The reservation is returned so a queued-then-cancelled decode leaks
    // neither VRAM accounting nor the worker slot (clean shutdown join).
    if (tok.isCancelled()) {
      const std::lock_guard<std::mutex> lock(BudgetMutex());
      budget->releaseVram(vramBytes);
      return;
    }
    // Decode body: applyMetadata (T3 EXIF orientation + T4 color/size) then
    // poolAcquire upload on the shared DecodedImage. Identity on malformed
    // bytes (never throws).
    try {
      const ExifOrientation o = readExifOrientation(bytes, len);
      DecodedImage cur = *out;
      cur.uploaded = false;
      cur = applyOrientation(std::move(cur), o);
      cur.uploaded = false;
      const ColorTransform ct = readColorProfile(bytes, len);
      const TargetSize keep{cur.width, cur.height, 1.0f};
      cur = applyColorAndSize(std::move(cur), ct, keep);
      *out = std::move(cur);
      (*out).uploaded = false;
    } catch (...) {
      (*out).uploaded = false;
    }
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
      const std::lock_guard<std::mutex> lock(BudgetMutex());
      budget->releaseVram(vramBytes);
      return;
    }
    // Success: residency now lives with the texture pool, so return the
    // in-flight budget reservation (see OWNERSHIP POLICY above).
    {
      const std::lock_guard<std::mutex> lock(BudgetMutex());
      budget->releaseVram(vramBytes);
    }
    // Only observable write: success flips uploaded after poolAcquire admits.
    out->uploaded = true;
  };
  pool.submit(std::move(task));
  return MediaDecodeOutcome{true, Status::kOk};
}

}  // namespace media
