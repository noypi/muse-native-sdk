#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include "core/thread/dispatcher.h"

namespace core {
namespace thread {

// Worker task priority, highest first: Render > Ui > Decode > Io.
// Minimal hermetic stand-in for the FR-CORE-003 work-stealing pool's
// priority lanes. Upgrade path: map these lanes onto the work-stealing
// pool's per-priority deques when FR-CORE-003 lands; the enum values and
// ordering contract stay unchanged.
enum class Priority { Render, Ui, Decode, Io };

// Cooperative cancellation token (T6 / AC-CORE-025).
//
// Copies share state: cancelling one view is observable through every
// other copy, including a copy captured by a task body. The pool never
// force-kills running tasks; bodies poll isCancelled() and exit early.
struct CancellationToken {
  CancellationToken() : state_(std::make_shared<std::atomic<bool>>(false)) {}

  void cancel() noexcept { state_->store(true, std::memory_order_relaxed); }
  bool isCancelled() const noexcept {
    return state_->load(std::memory_order_relaxed);
  }

 private:
  std::shared_ptr<std::atomic<bool>> state_;
};

// Unit of work for WorkerPool. `tok` is observed cooperatively by `fn`;
// destroying a Task (run, skipped, or dropped at shutdown) releases any
// buffers `fn` holds (RAII proof in task_cancellation_test).
struct Task {
  Priority pri = Priority::Io;
  CancellationToken tok;
  Fn fn;
};

}  // namespace thread
}  // namespace core
