#include "core/thread/worker_pool.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace core {
namespace thread {

namespace {

int rank(Priority p) noexcept {
  switch (p) {
    case Priority::Render:
      return 0;
    case Priority::Ui:
      return 1;
    case Priority::Decode:
      return 2;
    case Priority::Io:
      return 3;
  }
  return 4;
}

}  // namespace

struct WorkerPool::Impl {
  struct Item {
    int pri_rank;
    std::uint64_t seq;
    Task task;
  };

  std::mutex mu;
  std::condition_variable cv;
  std::vector<Item> heap;  // min-heap by (pri_rank, seq)
  std::vector<std::thread> threads;
  std::uint64_t next_seq = 0;
  bool stopping = false;
  bool shut = false;

  static bool higher(const Item& a, const Item& b) {
    // Returns true if a should run before b (for a min-heap view).
    if (a.pri_rank != b.pri_rank) return a.pri_rank < b.pri_rank;
    return a.seq < b.seq;
  }

  void push(Item item) {
    heap.push_back(std::move(item));
    std::size_t i = heap.size() - 1;
    while (i > 0) {
      std::size_t parent = (i - 1) / 2;
      if (higher(heap[i], heap[parent])) {
        std::swap(heap[i], heap[parent]);
        i = parent;
      } else {
        break;
      }
    }
  }

  bool pop(Task* out) {
    if (heap.empty()) return false;
    *out = std::move(heap.front().task);
    heap.front() = std::move(heap.back());
    heap.pop_back();
    std::size_t i = 0;
    for (;;) {
      std::size_t left = 2 * i + 1;
      std::size_t right = 2 * i + 2;
      std::size_t best = i;
      if (left < heap.size() && higher(heap[left], heap[best])) best = left;
      if (right < heap.size() && higher(heap[right], heap[best])) best = right;
      if (best == i) break;
      std::swap(heap[i], heap[best]);
      i = best;
    }
    return true;
  }

  void worker() {
    for (;;) {
      Task t;
      {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [this] { return stopping || !heap.empty(); });
        if (stopping && heap.empty()) return;
        if (!pop(&t)) continue;
      }
      // Cooperative cancellation: always invoke fn so the body observes
      // tok.isCancelled() itself and releases its captures on return.
      if (t.fn) t.fn();
    }
  }
};

WorkerPool::WorkerPool(std::size_t n) : impl_(new Impl()) {
  for (std::size_t i = 0; i < n; ++i) {
    impl_->threads.emplace_back([impl = impl_] { impl->worker(); });
  }
}

WorkerPool::~WorkerPool() {
  shutdown();
  delete impl_;
}

void WorkerPool::submit(Task t) {
  Impl::Item item{rank(t.pri), 0, std::move(t)};
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    if (impl_->shut || impl_->stopping) return;  // drop safely; Task dtor frees buffers
    item.seq = impl_->next_seq++;
    impl_->push(std::move(item));
  }
  impl_->cv.notify_one();
}

void WorkerPool::shutdown() noexcept {
  Impl* impl = impl_;
  {
    std::lock_guard<std::mutex> lock(impl->mu);
    if (impl->shut) return;
    impl->shut = true;
    impl->stopping = true;
    // Drop anything still queued so buffer owners are released promptly.
    impl->heap.clear();
  }
  impl->cv.notify_all();
  for (auto& th : impl->threads) {
    if (th.joinable()) th.join();
  }
}

}  // namespace thread
}  // namespace core
