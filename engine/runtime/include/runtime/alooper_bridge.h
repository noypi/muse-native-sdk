#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "runtime/gap_buffer.h"

// IME event bridge (FR-TEXT-008 / AC-TEXT-015). Hermetic seam-injected fake
// for ALooper/NDK platform calls: producers enqueue via post(), the looper
// thread applies via drain() synchronously in-process. No Android linkage,
// no JSI include/call anywhere; JSI absence is observable via
// BridgeStats::jsi_touches staying 0.
struct ImeEvent {
  enum class Kind { kCompose, kCommit, kDelete, kMoveCursor, kQuerySurrounding } kind;
  std::string text;
  int arg_a = 0;
  int arg_b = 0;
};

struct BridgeStats {
  int jsi_touches = 0;
  int events_delivered = 0;
  int events_dropped = 0;
};

class ALooperBridge {
 public:
  static constexpr int kMaxQueueDepth = 256;

  explicit ALooperBridge(GapBuffer& buf) : buf_(buf) {}

  bool post(const ImeEvent& ev) noexcept {
    try {
      if (static_cast<int>(queue_.size()) >= kMaxQueueDepth) {
        ++stats_.events_dropped;
        return false;
      }
      queue_.push_back(ev);
      return true;
    } catch (...) {
      // Never throws: allocation failure counts as a drop.
      ++stats_.events_dropped;
      return false;
    }
  }

  void drain() noexcept {
    try {
      while (!queue_.empty()) {
        const ImeEvent ev = queue_.front();
        queue_.pop_front();
        apply(ev);
        ++stats_.events_delivered;
      }
    } catch (...) {
      // Never throws: drain of remaining events stops on first failure.
    }
  }

  const BridgeStats& stats() const { return stats_; }

 private:
  void apply(const ImeEvent& ev) {
    switch (ev.kind) {
      case ImeEvent::Kind::kCompose:
      case ImeEvent::Kind::kCommit:
        if (!ev.text.empty()) {
          buf_.insert(buf_.byte_size(), ev.text);
        }
        break;
      case ImeEvent::Kind::kDelete: {
        if (ev.arg_a > 0) {
          const size_t size = buf_.byte_size();
          const size_t n = static_cast<size_t>(ev.arg_a) > size
                               ? size
                               : static_cast<size_t>(ev.arg_a);
          if (n > 0) {
            buf_.erase(size - n, n);
          }
        }
        break;
      }
      case ImeEvent::Kind::kMoveCursor: {
        const size_t a = ev.arg_a < 0 ? 0 : static_cast<size_t>(ev.arg_a);
        const size_t b = ev.arg_b < 0 ? 0 : static_cast<size_t>(ev.arg_b);
        buf_.set_cursors(std::vector<std::pair<size_t, size_t>>{{a, b}});
        break;
      }
      case ImeEvent::Kind::kQuerySurrounding:
        // Read-only query: no buffer mutation, delivery only.
        break;
    }
  }

  GapBuffer& buf_;
  std::deque<ImeEvent> queue_;
  BridgeStats stats_;
};
