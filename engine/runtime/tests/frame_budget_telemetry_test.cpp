// T7 contract tests — FR-RUNTIME-006 / AC-RUNTIME-016 (TC-RUNTIME-016).
// Frame callback on the JS (calling) thread with over-budget telemetry +
// owning component. Budget default 8ms.
// Hermetic: C++17 + committed runtime headers only; injected TelemetrySink +
// Clock seams (in-process fakes), no GPU, no Vulkan headers, no network, no
// env/config reads, no filesystem, no live services.
// Binds ONLY to the T7 `Interface:` line; never to implementation.
// RED STATE (expected): `runtime/frame.h` does not yet declare
// setFrameCallback/FrameCallback/FrameInfo (owned by the coder), so this file
// must fail to compile until the coder writes frame.h/frame.cpp.
#include <cstdint>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#include "runtime/frame.h"
#include "runtime/tree.h"

namespace {

// Same 3-level binary tree fixture as request_frame_test.cpp:
// root(0) -> {1, 2}; 1 -> {3, 4}; 2 -> {5, 6}.
void make_binary_tree(runtime::CompiledNode* nodes) {
  const int parents[7] = {-1, 0, 0, 1, 1, 2, 2};
  for (int i = 0; i < 7; ++i) {
    nodes[i].parent = parents[i];
    nodes[i].tag = 100 + i;
  }
}

bool instantiate_binary_tree(runtime::RuntimeTree& tree) {
  runtime::CompiledNode nodes[7];
  make_binary_tree(nodes);
  return runtime::instantiate(nodes, 7, tree) == 0 && tree.size() == 7;
}

// 8ms default budget in nanoseconds (A4).
constexpr std::uint64_t kDefaultBudgetNs = 8ULL * 1000ULL * 1000ULL;

// In-process fake clock: scripted nowNs() values, last value repeats.
// Drives actualNs deterministically; the OS clock is never touched.
class FakeClock : public runtime::Clock {
 public:
  explicit FakeClock(std::vector<std::uint64_t> script) : script_(script) {}
  std::uint64_t nowNs() const override {
    if (index_ < script_.size()) return script_[index_++];
    return script_.empty() ? 0 : script_.back();
  }

 private:
  std::vector<std::uint64_t> script_;
  mutable std::size_t index_ = 0;
};

// In-process recording telemetry sink: captures every report.
class FakeTelemetrySink : public runtime::TelemetrySink {
 public:
  void report(const runtime::FrameInfo& info) override { reported.push_back(info); }
  std::vector<runtime::FrameInfo> reported;
};

}  // namespace

// Named case: the frame callback runs synchronously in pumpFrame, i.e. on
// the calling (JS) thread — the callback thread id equals the caller thread
// id — and fires exactly once per presented frame.
static bool check_callback_runs_once_on_caller_thread() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  FakeClock clock({0, 1000});  // 1us frame: under budget.
  scheduler.setClock(&clock);
  const std::thread::id caller = std::this_thread::get_id();
  int calls = 0;
  std::thread::id seen;
  scheduler.setFrameCallback([&](runtime::FrameInfo info) {
    ++calls;
    seen = std::this_thread::get_id();
    if (info.budgetNs != kDefaultBudgetNs) calls = -100;
  });
  scheduler.requestFrame(11);
  if (scheduler.pumpFrame() != 1) return false;
  return calls == 1 && seen == caller;
}

// Named case: a pump with no callback set still presents normally —
// setFrameCallback is optional, never a crash, never a dropped frame.
static bool check_no_callback_pump_still_presents() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  scheduler.requestFrame(12);
  if (scheduler.pumpFrame() != 1) return false;
  return scheduler.framesPresented() == 1;
}

// Named case: an under-budget frame reports overBudget=false and emits no
// telemetry — the sink stays empty.
static bool check_under_budget_no_telemetry() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  FakeClock clock({0, 1000});  // 1us << 8ms.
  FakeTelemetrySink sink;
  scheduler.setClock(&clock);
  scheduler.setTelemetrySink(&sink);
  bool sawCallback = false;
  bool sawOverBudget = true;
  scheduler.setFrameCallback([&](runtime::FrameInfo info) {
    sawCallback = true;
    sawOverBudget = info.overBudget;
  });
  scheduler.requestFrame(13);
  if (scheduler.pumpFrame() != 1) return false;
  return sawCallback && !sawOverBudget && sink.reported.empty();
}

// Named case: an over-budget frame is reported to TelemetrySink together
// with the owning component — sink receives exactly one FrameInfo whose
// owner matches the requesting component and overBudget is true.
static bool check_over_budget_reports_with_owning_component() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  FakeClock clock({0, 20ULL * 1000ULL * 1000ULL});  // 20ms > 8ms.
  FakeTelemetrySink sink;
  scheduler.setClock(&clock);
  scheduler.setTelemetrySink(&sink);
  const runtime::ComponentId owner = 21;
  bool callbackOverBudget = false;
  scheduler.setFrameCallback(
      [&](runtime::FrameInfo info) { callbackOverBudget = info.overBudget; });
  scheduler.requestFrame(owner);
  if (scheduler.pumpFrame() != 1) return false;
  if (!callbackOverBudget) return false;
  if (sink.reported.size() != 1) return false;
  return sink.reported[0].owner == owner && sink.reported[0].overBudget &&
         sink.reported[0].actualNs > sink.reported[0].budgetNs;
}

// Named case: zero budget — any nonzero actual frame time is over budget
// and is reported with the owning component.
static bool check_zero_budget_reports() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  FakeClock clock({0, 1000});
  FakeTelemetrySink sink;
  scheduler.setClock(&clock);
  scheduler.setTelemetrySink(&sink);
  scheduler.setBudgetNs(0);
  scheduler.setFrameCallback([](runtime::FrameInfo) {});
  scheduler.requestFrame(22);
  if (scheduler.pumpFrame() != 1) return false;
  return sink.reported.size() == 1 && sink.reported[0].overBudget &&
         sink.reported[0].owner == 22;
}

// Named case: the default budget is 8ms — a fresh scheduler reports
// budgetNs == 8,000,000 in the callback FrameInfo without any setter call.
static bool check_default_budget_is_8ms() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  FakeClock clock({0, 1000});
  scheduler.setClock(&clock);
  std::uint64_t seenBudget = 0;
  bool sawCallback = false;
  scheduler.setFrameCallback([&](runtime::FrameInfo info) {
    sawCallback = true;
    seenBudget = info.budgetNs;
  });
  scheduler.requestFrame(23);
  if (scheduler.pumpFrame() != 1) return false;
  return sawCallback && seenBudget == kDefaultBudgetNs;
}

// Named case: continuous driver frames also invoke the callback every pump
// (one call per presented frame), so a runaway loop's cost is visible.
static bool check_continuous_frames_invoke_callback_each_pump() {
  runtime::RuntimeTree tree;
  if (!instantiate_binary_tree(tree)) return false;
  runtime::FrameScheduler scheduler(tree);
  FakeClock clock({0, 1000, 2000, 3000, 4000, 5000});
  scheduler.setClock(&clock);
  int calls = 0;
  scheduler.setFrameCallback([&](runtime::FrameInfo) { ++calls; });
  scheduler.setContinuous(31, true);
  if (scheduler.pumpFrame() != 1) return false;
  if (scheduler.pumpFrame() != 1) return false;
  scheduler.releaseOwner(31);
  return calls == 2;
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_callback_runs_once_on_caller_thread);
  RUN_CASE(check_no_callback_pump_still_presents);
  RUN_CASE(check_under_budget_no_telemetry);
  RUN_CASE(check_over_budget_reports_with_owning_component);
  RUN_CASE(check_zero_budget_reports);
  RUN_CASE(check_default_budget_is_8ms);
  RUN_CASE(check_continuous_frames_invoke_callback_each_pump);
  if (failures > 0) {
    std::printf("frame_budget_telemetry_test: %d failing\n", failures);
    return 1;
  }
  std::printf("frame_budget_telemetry_test: all passing\n");
  return 0;
}
