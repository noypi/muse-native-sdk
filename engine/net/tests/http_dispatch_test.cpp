// Batch 1 contract tests — T3 dispatch-budget probe
// (FR-NET-004 / AC-NET-007 / TC-NET-007 unit half).
//
// Hermetic: C++17 only, no engine headers required. The probe under test is
// the production seam declared below; the coder provides its definition
// (ownership: coder, any engine/net source file it chooses). Until then this
// file compiles but does not link — that undefined-reference failure is the
// expected red state (see unit-tests/T3.md).
//
// Why declarations live here: the plan lists no probe header for T3 (the
// <1ms budget is owned by perf; T3 ships only a unit probe plus the external
// device-lab note), so this test binds directly to the T3 `Interface:` line:
//
//   void RecordDispatchNanos(uint64_t ns); bool WithinBudget(uint64_t ns);
//
// ASSUMPTIONS (reconciliation: update call sites, not expectations):
//   A-NS:   the probe lives in `muse::net` with exactly these signatures.
//   A-GATE: the budget gate is strictly sub-millisecond: any ns < 1,000,000
//           is within budget; 1,000,000 ns and above is not.
//   A-REC:  RecordDispatchNanos is a pure observation hook — it never
//           throws, never blocks, never performs I/O.
// Reuse evidence for the AC-NET-007 "connection reuse observed" half comes
// from the T2 pool bookkeeping cases in transport_api_test.cpp plus the
// on-device gate in device-lab/http-dispatch/README.md; this probe covers
// only the dispatch-budget half at unit level.
#include <cstdint>
#include <cstdio>

namespace muse {
namespace net {

// Production seam under test (coder-owned definition).
void RecordDispatchNanos(uint64_t ns);
bool WithinBudget(uint64_t ns);

}  // namespace net
}  // namespace muse

namespace {

constexpr uint64_t kBudgetNanos = 1000000ULL;  // 1ms, per A-GATE

bool check_zero_nanos_is_within_budget() {
  return muse::net::WithinBudget(0) == true;
}

bool check_typical_dispatch_is_within_budget() {
  // 250us: comfortably inside the <1ms JS-to-native dispatch budget.
  return muse::net::WithinBudget(250000) == true;
}

bool check_just_under_1ms_is_within_budget() {
  return muse::net::WithinBudget(kBudgetNanos - 1) == true;
}

bool check_exactly_1ms_is_outside_budget() {
  // A-GATE boundary: the budget is *under* 1ms, so 1,000,000ns fails.
  return muse::net::WithinBudget(kBudgetNanos) == false;
}

bool check_over_budget_value_is_outside_budget() {
  return muse::net::WithinBudget(5000000) == false;
}

bool check_record_never_throws_and_accepts_any_sample() {
  // A-REC: observation only — 0, budget-edge, and huge values all record
  // without throwing or affecting the gate.
  muse::net::RecordDispatchNanos(0);
  muse::net::RecordDispatchNanos(kBudgetNanos - 1);
  muse::net::RecordDispatchNanos(kBudgetNanos);
  muse::net::RecordDispatchNanos(UINT64_C(18446744073709551000));
  return muse::net::WithinBudget(10) == true &&
         muse::net::WithinBudget(kBudgetNanos) == false;
}

}  // namespace

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_zero_nanos_is_within_budget);
  RUN_CASE(check_typical_dispatch_is_within_budget);
  RUN_CASE(check_just_under_1ms_is_within_budget);
  RUN_CASE(check_exactly_1ms_is_outside_budget);
  RUN_CASE(check_over_budget_value_is_outside_budget);
  RUN_CASE(check_record_never_throws_and_accepts_any_sample);
  if (failures > 0) {
    std::printf("http_dispatch_test: %d failing\n", failures);
    return 1;
  }
  std::printf("http_dispatch_test: all passing\n");
  return 0;
}
