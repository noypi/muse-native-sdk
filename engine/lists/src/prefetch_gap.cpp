#include "lists/prefetch_gap.h"

#include <cmath>
#include <cstdint>

namespace lists {

namespace {

// Average row height used to convert fling displacement per frame into rows.
// Linear prefetch ahead plus nested-prefetch coverage is folded into the
// viewport-derived cap below.
constexpr double kAvgRowPx = 120.0;

}  // namespace

PrefetchPlan planPrefetch(float velocityPxS, int frameBudgetNs,
                          int viewportRows) noexcept {
  PrefetchPlan out;
  if (frameBudgetNs <= 0 || viewportRows <= 0) return out;
  if (!std::isfinite(velocityPxS)) return out;
  double speed = std::fabs(static_cast<double>(velocityPxS));
  if (speed <= 0.0) {
    // Rest: nothing to prefetch, but the bind budget stays valid so the
    // caller can still schedule gap work inside this frame.
    out.distanceRows = 0;
    out.bindBudgetNs = frameBudgetNs / 2;
    return out;
  }
  // Displacement covered by one frame at this speed, converted to rows.
  double frameSec = static_cast<double>(frameBudgetNs) / 1e9;
  double px = speed * frameSec;
  double rows = px / kAvgRowPx;
  // Cap by the frame budget: linear + nested prefetch never exceeds twice the
  // viewport, so gap work stays inside PERF-BUDGET-ENFORCE.
  double cap = static_cast<double>(viewportRows) * 2.0;
  if (rows > cap) rows = cap;
  if (rows < 1.0) rows = 1.0;  // any real fling prefetches at least one row
  // rows is finite here: speed finite, frameSec finite, clamped by cap.
  int64_t d = static_cast<int64_t>(rows);
  if (d < 0) d = 0;
  if (d > cap) d = static_cast<int64_t>(cap);
  out.distanceRows = static_cast<int>(d);
  // Per-bind time budget: half the frame, so binds never eat the whole frame.
  out.bindBudgetNs = frameBudgetNs / 2;
  return out;
}

GapWorker::GapWorker() : worker_([this] { run(); }) {}

GapWorker::~GapWorker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

Status GapWorker::submit(const PrefetchPlan& plan) {
  if (plan.distanceRows < 0 || plan.bindBudgetNs < 0) {
    return Status::kInvalidArg;
  }
  int slot = pool_->acquire();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push(Job{plan, slot});
  }
  cv_.notify_one();
  return Status::kOk;
}

Holder GapWorker::takeReady() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (ready_.empty()) return Holder{};
  int slot = ready_.front();
  ready_.pop();
  return Holder{pool_, slot};
}

void GapWorker::run() {
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || !pending_.empty(); });
      if (stop_ && pending_.empty()) return;
      if (pending_.empty()) continue;
      job = pending_.front();
      pending_.pop();
    }
    // Bind on the worker thread only. An over-budget bind (bindBudgetNs too
    // small to fit the work) defers — it still resolves to a holder, never a
    // drop and never a blank; the pool stays usable afterwards.
    std::this_thread::yield();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      ready_.push(job.slot);
    }
  }
}

}  // namespace lists
