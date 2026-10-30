#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace lists {

// Prefetch plan for a fling: how many rows to prefetch ahead and the per-bind
// time budget (ns) each prefetched row may consume. Produced by planPrefetch;
// consumed by GapWorker::submit.
struct PrefetchPlan {
  int distanceRows = 0;
  int bindBudgetNs = 0;
};

enum class Status { kOk, kInvalidArg };

// Component-type-keyed holder pool. A single GapWorker serves one component
// type, so one free-list is the whole pool; dropping a holder without entering
// it returns its slot here on destruction (RAII) with zero visible cost.
struct GapPool {
  std::mutex mutex;
  std::vector<int> free;
  int next = 0;
  int acquire() {
    std::lock_guard<std::mutex> lock(mutex);
    if (!free.empty()) {
      int s = free.back();
      free.pop_back();
      return s;
    }
    return next++;
  }
  void release(int slot) {
    if (slot < 0) return;
    std::lock_guard<std::mutex> lock(mutex);
    free.push_back(slot);
  }
};

// Pooled holder for a prefetched row. Valid == ready to bind. Dropping a
// holder without entering it returns its slot to the pool on destruction
// (RAII) with zero visible cost — the miss path never blanks. Move-only so a
// slot has exactly one owner.
class Holder {
 public:
  Holder() = default;
  Holder(const Holder&) = delete;
  Holder& operator=(const Holder&) = delete;
  Holder(Holder&& other) noexcept
      : pool_(std::move(other.pool_)), slot_(other.slot_) {
    other.slot_ = -1;
  }
  Holder& operator=(Holder&& other) noexcept {
    if (this != &other) {
      release();
      pool_ = std::move(other.pool_);
      slot_ = other.slot_;
      other.slot_ = -1;
    }
    return *this;
  }
  ~Holder() { release(); }

  explicit operator bool() const { return slot_ >= 0; }

 private:
  friend class GapWorker;
  Holder(std::shared_ptr<GapPool> pool, int slot)
      : pool_(std::move(pool)), slot_(slot) {}
  void release() {
    if (slot_ >= 0 && pool_) pool_->release(slot_);
    slot_ = -1;
    pool_.reset();
  }

  std::shared_ptr<GapPool> pool_;
  int slot_ = -1;
};

// Velocity-derived prefetch distance capped by the frame budget, plus the
// per-bind time budget. Pure function; never throws. Fail-closed: non-positive
// frameBudgetNs, non-positive viewportRows, or non-finite velocity yield a
// zeroed plan. Rest (velocity 0) prefetches zero rows. Distance is symmetric
// in the fling sign — direction selects which side, speed selects how far.
PrefetchPlan planPrefetch(float velocityPxS, int frameBudgetNs,
                          int viewportRows) noexcept;

// Core-pool worker: prefetch/bind runs on the internal worker thread, the UI
// thread only polls takeReady. Default-constructs its worker, non-copyable,
// joins on destruction. submit is callable from any thread; takeReady never
// blocks — it returns an invalid Holder when nothing is ready yet.
class GapWorker {
 public:
  GapWorker();
  ~GapWorker();
  GapWorker(const GapWorker&) = delete;
  GapWorker& operator=(const GapWorker&) = delete;

  // Enqueue a prefetch plan for background bind. Rejects negative distances
  // (fail-closed) with kInvalidArg. Over-budget binds (bindBudgetNs too small
  // to fit any bind) defer instead of dropping: submit still returns kOk and a
  // later takeReady delivers the holder.
  Status submit(const PrefetchPlan& plan);

  // Non-blocking poll for a ready holder. Returns an invalid Holder when
  // nothing is ready — the caller spins/yields without stalling.
  Holder takeReady();

 private:
  struct Job {
    PrefetchPlan plan;
    int slot = -1;
  };

  void run();

  std::shared_ptr<GapPool> pool_ = std::make_shared<GapPool>();
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<Job> pending_;
  std::queue<int> ready_;
  bool stop_ = false;
  std::thread worker_;
};

}  // namespace lists
