#pragma once

#include <cstddef>

#include "core/thread/task.h"

namespace core {
namespace thread {

// Minimal hermetic worker pool standing in for the FR-CORE-003
// std::jthread work-stealing pool (T6 / AC-CORE-025).
//
// Fixed thread count, priority-ordered queue (Render > Ui > Decode > Io,
// FIFO within a priority). Cancellation is cooperative: queued or running
// tasks observe Task::tok; the pool always invokes fn and lets the body
// skip work itself. submit() after shutdown() drops the task safely;
// shutdown() is idempotent and joins all workers.
//
// A count of n == 0 is allowed but discouraged: no worker threads run, so
// submitted tasks stay queued until shutdown() drops them. Prefer n >= 1
// for any pool expected to execute work.
//
// Upgrade path to FR-CORE-003: replace the single central mutex+heap with
// per-worker Chase-Lev deques and work stealing; keep Task, Priority, and
// CancellationToken unchanged. Implemented on std::thread (C++17) with the
// same stop-on-shutdown semantics std::jthread would provide.
class WorkerPool {
 public:
  explicit WorkerPool(std::size_t n);
  ~WorkerPool();
  WorkerPool(const WorkerPool&) = delete;
  WorkerPool& operator=(const WorkerPool&) = delete;

  void submit(Task t);
  void shutdown() noexcept;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace thread
}  // namespace core
