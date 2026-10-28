#pragma once

#include <cstddef>
#include <functional>

namespace core {
namespace thread {

using Fn = std::function<void()>;

// Batched frame-aligned main-thread dispatcher (T5 / AC-CORE-024).
//
// Non-immediate posts batch until drainFrame(), called at the FR-RUNTIME-001
// frame boundary. immediate=true bypasses batching and runs inline.
// FIFO order within a batch. post() is thread-safe; drainFrame() is called
// on the main thread only. Every callback runs wrapped in a LocalRefScope.
class Dispatcher {
 public:
  Dispatcher();
  ~Dispatcher();
  Dispatcher(const Dispatcher&) = delete;
  Dispatcher& operator=(const Dispatcher&) = delete;

  void post(Fn f, bool immediate = false) noexcept;
  void drainFrame() noexcept;
  std::size_t pending() const noexcept;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace thread
}  // namespace core
