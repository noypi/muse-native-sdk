// Batch 3 contract tests — FR-NET-011 / AC-NET-021 (TC-NET-021).
// Covers T4 (ReactiveQuery per-frame coalescing).
// Hermetic: C++17 + net headers only; no network, no env/config reads,
// no live services, no threads/sleeps. Frame time comes from an injected
// in-process FakeClock; callbacks are recorded inline.
// Binds ONLY to the T4 `Interface:` line; never to implementation.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

#include "net/reactive_query.h"

namespace {

// In-process fake frame clock behind the IFrameClock seam. Test drives the
// frame id explicitly; no vsync, no sleeps, no wall clock.
class FakeClock : public muse::net::IFrameClock {
 public:
  explicit FakeClock(uint64_t frame = 0) : frame_(frame) {}
  uint64_t frameId() const override { return frame_; }
  void setFrame(uint64_t f) { frame_ = f; }

 private:
  uint64_t frame_ = 0;
};

std::shared_ptr<FakeClock> makeClock(uint64_t frame = 0) {
  return std::make_shared<FakeClock>(frame);
}

}  // namespace

// Named case: a single notifyChanged delivers exactly one callback carrying
// the current frame token.
static bool check_single_notify_delivers_current_frame() {
  auto clock = makeClock(7);
  muse::net::ReactiveQuery q(clock);
  std::vector<uint64_t> got;
  q.subscribe([&](uint64_t frame) { got.push_back(frame); });
  const uint64_t ret = q.notifyChanged();
  if (ret != 7) return false;
  if (got.size() != 1) return false;
  return got[0] == 7;
}

// Named case: N row changes in one frame deliver exactly one subscriber
// invocation (coalesced).
static bool check_many_notifies_one_frame_coalesced_to_one() {
  auto clock = makeClock(3);
  muse::net::ReactiveQuery q(clock);
  int calls = 0;
  std::vector<uint64_t> got;
  q.subscribe([&](uint64_t frame) {
    ++calls;
    got.push_back(frame);
  });
  for (int i = 0; i < 10; ++i) q.notifyChanged();
  if (calls != 1) return false;
  if (got.size() != 1 || got[0] != 3) return false;
  return true;
}

// Named case: notifyChanged returns the current frame id (change token).
static bool check_notify_returns_frame_token() {
  auto clock = makeClock(42);
  muse::net::ReactiveQuery q(clock);
  q.subscribe([](uint64_t) {});
  if (q.notifyChanged() != 42) return false;
  clock->setFrame(43);
  if (q.notifyChanged() != 43) return false;
  return true;
}

// Named case: changes across frames deliver one callback per frame, in order.
static bool check_changes_across_frames_one_per_frame_in_order() {
  auto clock = makeClock(1);
  muse::net::ReactiveQuery q(clock);
  std::vector<uint64_t> got;
  q.subscribe([&](uint64_t frame) { got.push_back(frame); });
  q.notifyChanged();  // frame 1
  clock->setFrame(2);
  q.notifyChanged();  // frame 2
  clock->setFrame(3);
  q.notifyChanged();  // frame 3
  if (got.size() != 3) return false;
  return got[0] == 1 && got[1] == 2 && got[2] == 3;
}

// Named case: no polling — with no notifyChanged, the subscriber is never
// invoked even as the frame advances.
static bool check_no_notify_no_callback_no_polling() {
  auto clock = makeClock(1);
  muse::net::ReactiveQuery q(clock);
  int calls = 0;
  q.subscribe([&](uint64_t) { ++calls; });
  clock->setFrame(2);
  clock->setFrame(3);
  return calls == 0;
}

// Named case: next frame re-arms after coalescing — two notifies in frame 1
// (one delivery), then one notify in frame 2 (second delivery).
static bool check_next_frame_rearms_after_coalesce() {
  auto clock = makeClock(1);
  muse::net::ReactiveQuery q(clock);
  std::vector<uint64_t> got;
  q.subscribe([&](uint64_t frame) { got.push_back(frame); });
  q.notifyChanged();
  q.notifyChanged();  // coalesced
  clock->setFrame(2);
  q.notifyChanged();
  if (got.size() != 2) return false;
  return got[0] == 1 && got[1] == 2;
}

// Named case: frame-zero boundary — notify at frame 0 still delivers frame 0.
// ASSUMPTION (A-T4-zero): frame 0 is a valid frame, not a sentinel.
static bool check_frame_zero_delivers() {
  auto clock = makeClock(0);
  muse::net::ReactiveQuery q(clock);
  std::vector<uint64_t> got;
  q.subscribe([&](uint64_t frame) { got.push_back(frame); });
  if (q.notifyChanged() != 0) return false;
  if (got.size() != 1 || got[0] != 0) return false;
  return true;
}

// Named case: multiple subscribers each observe the coalesced token.
// ASSUMPTION (A-T4-multi): subscribe is additive — every subscriber is
// invoked once per frame. If the contract settles on replace-semantics,
// this case documents the dispute (GR-08 T1.10 route).
static bool check_multiple_subscribers_each_get_one_per_frame() {
  auto clock = makeClock(5);
  muse::net::ReactiveQuery q(clock);
  std::vector<uint64_t> a, b;
  q.subscribe([&](uint64_t frame) { a.push_back(frame); });
  q.subscribe([&](uint64_t frame) { b.push_back(frame); });
  q.notifyChanged();
  q.notifyChanged();  // coalesced
  if (a.size() != 1 || b.size() != 1) return false;
  return a[0] == 5 && b[0] == 5;
}

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_single_notify_delivers_current_frame);
  RUN_CASE(check_many_notifies_one_frame_coalesced_to_one);
  RUN_CASE(check_notify_returns_frame_token);
  RUN_CASE(check_changes_across_frames_one_per_frame_in_order);
  RUN_CASE(check_no_notify_no_callback_no_polling);
  RUN_CASE(check_next_frame_rearms_after_coalesce);
  RUN_CASE(check_frame_zero_delivers);
  RUN_CASE(check_multiple_subscribers_each_get_one_per_frame);
  if (failures > 0) {
    std::printf("reactive_query_test: %d failing\n", failures);
    return 1;
  }
  std::printf("reactive_query_test: all passing\n");
  return 0;
}
