// T4 contract tests — FR-NAV-007 / AC-NAV-019 (TC-NAV-019).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services.
// Binds to the plan T4 `Interface:` line:
//   `ResultDelivery deliverResult(ResultHost& h,
//    const std::string& toRoute, const TypedParam& value);`
//   `void onPopped(ResultHost& h, const std::string& routeId);`
// with `ResultDelivery{delivered:bool, error:ResultError}`,
// `ResultError{NONE, TARGET_GONE, TYPE_MISMATCH}`.
// `TypedParam`/`ParamType` are reused from `navigation/trie.h`; this file
// declares no competing enum.
//
// ASSUMPTION (ResultHost registration): the plan names `ResultHost` but
// does not pin how a live destination and its declared result type are
// registered. Tests use `h.registerDestination(routeId, type, callback)`
// where `callback` is `std::function<void(const TypedParam&)>`, and
// `h.isLive(routeId)` is NOT required. If the coder names registration
// differently, update the call sites — not the expected behavior.
// ASSUMPTION (delivery to never-registered route): treated as
// TARGET_GONE (the destination is not on the back stack), no callback.
#include <cstdio>
#include <functional>
#include <string>

#include "navigation/result_channel.h"
#include "navigation/trie.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

TypedParam makeString(const std::string& s) {
  TypedParam p;
  p.type = ParamType::String;
  p.stringValue = s;
  return p;
}

TypedParam makeInt(long long v) {
  TypedParam p;
  p.type = ParamType::Int;
  p.intValue = v;
  return p;
}

bool check_live_destination_delivers_typed_value() {
  ResultHost h;
  std::string seen;
  bool fired = false;
  h.registerDestination(
      "editor", ParamType::String,
      [&](const TypedParam& v) {
        fired = true;
        seen = v.stringValue;
      });
  ResultDelivery d = deliverResult(h, "editor", makeString("ok-result"));
  return d.delivered && d.error == ResultError::NONE && fired &&
         seen == "ok-result";
}

bool check_type_mismatch_rejected_no_callback() {
  ResultHost h;
  bool fired = false;
  h.registerDestination(
      "editor", ParamType::String,
      [&](const TypedParam&) { fired = true; });
  ResultDelivery d = deliverResult(h, "editor", makeInt(42));
  return !d.delivered && d.error == ResultError::TYPE_MISMATCH && !fired;
}

bool check_popped_receiver_returns_target_gone() {
  ResultHost h;
  bool fired = false;
  h.registerDestination(
      "editor", ParamType::String,
      [&](const TypedParam&) { fired = true; });
  onPopped(h, "editor");
  ResultDelivery d = deliverResult(h, "editor", makeString("late"));
  return !d.delivered && d.error == ResultError::TARGET_GONE && !fired;
}

bool check_late_result_after_pop_fires_no_stale_callback() {
  ResultHost h;
  int calls = 0;
  h.registerDestination(
      "editor", ParamType::String,
      [&](const TypedParam&) { ++calls; });
  ResultDelivery first = deliverResult(h, "editor", makeString("first"));
  onPopped(h, "editor");
  ResultDelivery late = deliverResult(h, "editor", makeString("second"));
  return first.delivered && !late.delivered &&
         late.error == ResultError::TARGET_GONE && calls == 1;
}

bool check_unknown_route_returns_target_gone() {
  ResultHost h;
  ResultDelivery d = deliverResult(h, "ghost", makeString("x"));
  return !d.delivered && d.error == ResultError::TARGET_GONE;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_live_destination_delivers_typed_value(),
              "live_destination_delivers_typed_value");
  EXPECT_TRUE(check_type_mismatch_rejected_no_callback(),
              "type_mismatch_rejected_no_callback");
  EXPECT_TRUE(check_popped_receiver_returns_target_gone(),
              "popped_receiver_returns_target_gone");
  EXPECT_TRUE(check_late_result_after_pop_fires_no_stale_callback(),
              "late_result_after_pop_fires_no_stale_callback");
  EXPECT_TRUE(check_unknown_route_returns_target_gone(),
              "unknown_route_returns_target_gone");
  if (failures > 0) {
    std::printf("result_channel_test: %d failing\n", failures);
    return 1;
  }
  std::printf("result_channel_test: all passing\n");
  return 0;
}
