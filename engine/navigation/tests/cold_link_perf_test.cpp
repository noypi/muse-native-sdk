// T7 contract tests — FR-NAV-005 / AC-NAV-012 (in-repo proxy for external
// TC-NAV-012 device-lab). Hermetic: C++17 + navigation headers only; no
// network, no env/config reads, no filesystem, no live services.
// Consults no implementation; binds to the T7 `Interface:` line verbatim
// (see unit-tests/T7.md).
//
// ASSUMPTION (manifest): `resolveColdLink(uri)` takes no route table, so
// these cases assume the coder wires a default compiled manifest
// containing at least routes "home" and "details" reachable as
// "muse://home" and "muse://details". Unknown hosts/paths must yield
// NO_MATCH; empty or syntactically invalid URIs must yield INVALID.
// ASSUMPTION (budget): "transition budget <4ms" means
// `withinBudget == (micros < 4000)`.
// ASSUMPTION (chain): a nested cold link (e.g. "muse://details/42")
// pushes the full parent chain as one transaction, so the resulting
// stack blob contains both "home" and "details".
#include <cstdio>
#include <string>

#include "navigation/cold_link.h"
#include "navigation/predictive_back.h"

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

bool check_cold_link_home_ok_no_js() {
  ColdLinkResult r = resolveColdLink("muse://home");
  return r.ok && r.error == ColdLinkError::NONE && !r.jsExecuted &&
         !r.stack.graphBlob.empty();
}

bool check_cold_link_nested_pushes_parent_chain() {
  ColdLinkResult r = resolveColdLink("muse://details/42");
  if (!r.ok || r.error != ColdLinkError::NONE) return false;
  if (r.jsExecuted) return false;
  const std::string& blob = r.stack.graphBlob;
  return blob.find("home") != std::string::npos &&
         blob.find("details") != std::string::npos;
}

bool check_cold_link_always_reports_js_false() {
  ColdLinkResult ok = resolveColdLink("muse://home");
  ColdLinkResult miss = resolveColdLink("muse://no_such_route_xyz");
  return !ok.jsExecuted && !miss.jsExecuted;
}

bool check_cold_link_no_match() {
  ColdLinkResult r = resolveColdLink("muse://no_such_route_xyz");
  return !r.ok && r.error == ColdLinkError::NO_MATCH;
}

bool check_cold_link_invalid_empty() {
  ColdLinkResult r = resolveColdLink("");
  return !r.ok && r.error == ColdLinkError::INVALID;
}

bool check_cold_link_invalid_malformed() {
  ColdLinkResult r = resolveColdLink("://missing-scheme");
  return !r.ok && r.error == ColdLinkError::INVALID;
}

bool check_measure_trivial_fn_within_budget() {
  PerfSample s = measureTransition([] {});
  return s.micros >= 0 && s.withinBudget == (s.micros < 4000) &&
         s.withinBudget;
}

bool check_measure_budget_flag_consistent() {
  volatile int sink = 0;
  PerfSample s = measureTransition([&] {
    for (int i = 0; i < 100; ++i) sink += i;
  });
  (void)sink;
  return s.micros >= 0 && s.withinBudget == (s.micros < 4000);
}

bool check_predictive_progress_visible_before_lift() {
  // Scripted stream (T3): progress is synchronously visible before the
  // measured transition ("lift") runs.
  PredictiveBack pb;
  pb.onProgress(BackProgress{0.5f, 0, false});
  if (pb.transitionProgress() != 0.5f) return false;
  PerfSample s = measureTransition([] {});
  return s.withinBudget && pb.transitionProgress() == 0.5f;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_cold_link_home_ok_no_js(), "cold resolve home ok with jsExecuted==false");
  EXPECT_TRUE(check_cold_link_nested_pushes_parent_chain(),
              "nested cold link pushes full parent chain as one transaction");
  EXPECT_TRUE(check_cold_link_always_reports_js_false(),
              "cold resolve always reports jsExecuted==false");
  EXPECT_TRUE(check_cold_link_no_match(), "unknown cold link returns NO_MATCH");
  EXPECT_TRUE(check_cold_link_invalid_empty(), "empty uri returns INVALID");
  EXPECT_TRUE(check_cold_link_invalid_malformed(), "malformed uri returns INVALID");
  EXPECT_TRUE(check_measure_trivial_fn_within_budget(),
              "trivial transition within <4ms budget");
  EXPECT_TRUE(check_measure_budget_flag_consistent(),
              "withinBudget consistent with micros < 4000");
  EXPECT_TRUE(check_predictive_progress_visible_before_lift(),
              "predictive progress visible before lift via scripted stream");
  std::printf(failures == 0 ? "ALL PASS\n" : "FAILURES %d\n", failures);
  return failures == 0 ? 0 : 1;
}
