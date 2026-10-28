#include "core/thread/dispatcher.h"

#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/thread/main_checker.h"

namespace core {
namespace thread {

// --- MainThreadChecker + LocalRefScope state (header-only seam, defined here
// so T5 ships a single translation unit: src/dispatcher.cpp) ---

namespace {
struct CheckerState {
  std::mutex mutex;
  std::thread::id mainId;
  bool hasMainId = false;
  std::size_t count = 0;
  std::string last;
};
CheckerState& checkerState() {
  static CheckerState s;
  return s;
}
}  // namespace

void MainThreadChecker::setMainThreadId(std::thread::id id) noexcept {
  try {
    CheckerState& s = checkerState();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.mainId = id;
    s.hasMainId = true;
  } catch (...) {
  }
}

void MainThreadChecker::check(const char* what) noexcept {
  try {
    CheckerState& s = checkerState();
    std::lock_guard<std::mutex> lock(s.mutex);
    const bool onMain =
        s.hasMainId && (std::this_thread::get_id() == s.mainId);
    if (onMain) return;
    ++s.count;
    s.last = what ? what : "";
#ifndef NDEBUG
    std::fprintf(stderr, "[MainThreadChecker] off-main UI call: %s\n",
                 what ? what : "(null)");
#endif
  } catch (...) {
  }
}

std::size_t MainThreadChecker::violations() noexcept {
  try {
    CheckerState& s = checkerState();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.count;
  } catch (...) {
    return 0;
  }
}

std::string MainThreadChecker::lastWhat() noexcept {
  try {
    CheckerState& s = checkerState();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.last;
  } catch (...) {
    return "";
  }
}

void MainThreadChecker::resetForTest() noexcept {
  try {
    CheckerState& s = checkerState();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.count = 0;
    s.last.clear();
  } catch (...) {
  }
}

namespace {
thread_local int t_scopeDepth = 0;
}

LocalRefScope::LocalRefScope() noexcept { ++t_scopeDepth; }
LocalRefScope::~LocalRefScope() noexcept {
  if (t_scopeDepth > 0) --t_scopeDepth;
}
int LocalRefScope::depth() noexcept { return t_scopeDepth; }

struct Dispatcher::Impl {
  mutable std::mutex mutex;
  std::deque<Fn> queue;
};

Dispatcher::Dispatcher() : impl_(new Impl()) {}
Dispatcher::~Dispatcher() { delete impl_; }

void Dispatcher::post(Fn f, bool immediate) {
  if (!f) return;
  if (immediate) {
    // Bypass batching: run inline, still inside a LocalRefScope.
    try {
      LocalRefScope scope;
      f();
    } catch (...) {
    }
    return;
  }
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->queue.push_back(std::move(f));
  } catch (...) {
  }
}

void Dispatcher::drainFrame() noexcept {
  std::vector<Fn> batch;
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    batch.reserve(impl_->queue.size());
    while (!impl_->queue.empty()) {
      batch.push_back(std::move(impl_->queue.front()));
      impl_->queue.pop_front();
    }
  } catch (...) {
    return;
  }
  for (Fn& f : batch) {
    try {
      LocalRefScope scope;
      f();
    } catch (...) {
    }
  }
}

std::size_t Dispatcher::pending() const noexcept {
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->queue.size();
  } catch (...) {
    return 0;
  }
}

}  // namespace thread
}  // namespace core
