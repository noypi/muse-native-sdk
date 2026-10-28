// Batch 2 (FR-STATE-005) contract tests — T4 background-sync scheduler seam.
// Spec refs: FR-STATE-005, AC-STATE-023 (TC-STATE-023), assumption A4.
// Hermetic: C++17 + committed engine/state headers only; in-process fake
// scheduler, no live process, no env/config/network.
// Binds ONLY to the T4 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T4.md):
//   A-SC1: namespace muse::state; SyncRequest is an aggregate
//          { SyncConstraint constraint; bool requiresDozeExemption; }.
//   A-SC2: FakeScheduler IS-A SyncScheduler (public inheritance), so a
//          FakeScheduler binds to a SyncScheduler& seam; schedule() on the
//          base dispatches to the fake. Default-constructible.
//   A-SC3: last() on an empty scheduler returns Status::kNotFound, never
//          crashes; SyncRequest compares field-wise in-test.
#include <cstdio>

#include "muse/state/atom.hpp"
#include "muse/state/sync_scheduler.hpp"

namespace {

using muse::state::FakeScheduler;
using muse::state::Status;
using muse::state::SyncConstraint;
using muse::state::SyncRequest;
using muse::state::SyncScheduler;

static bool same_request(const SyncRequest& a, const SyncRequest& b) {
  return a.constraint == b.constraint &&
         a.requiresDozeExemption == b.requiresDozeExemption;
}

static bool check_last_on_empty_returns_not_found() {
  FakeScheduler s;
  auto r = s.last();
  return !r.ok() && r.status() == Status::kNotFound;
}

static bool check_schedule_any_is_recorded() {
  FakeScheduler s;
  SyncRequest req{SyncConstraint::kAny, false};
  if (s.schedule(req) != Status::kOk) return false;
  auto got = s.last();
  return got.ok() && same_request(got.value(), req);
}

static bool check_schedule_unmetered_with_doze_exemption_is_recorded() {
  FakeScheduler s;
  SyncRequest req{SyncConstraint::kUnmetered, true};
  if (s.schedule(req) != Status::kOk) return false;
  auto got = s.last();
  return got.ok() && same_request(got.value(), req);
}

static bool check_schedule_idle_under_doze_defers_without_live_process() {
  // AC-STATE-023: scheduling under Doze-idle defers to the matrix fake;
  // no path requires a running process. The fake records kOk and the
  // request carries the idle constraint + Doze exemption.
  FakeScheduler s;
  SyncRequest req{SyncConstraint::kIdle, true};
  if (s.schedule(req) != Status::kOk) return false;
  auto got = s.last();
  if (!got.ok()) return false;
  return got.value().constraint == SyncConstraint::kIdle &&
         got.value().requiresDozeExemption;
}

static bool check_schedule_idle_without_exemption_is_recorded() {
  FakeScheduler s;
  SyncRequest req{SyncConstraint::kIdle, false};
  if (s.schedule(req) != Status::kOk) return false;
  auto got = s.last();
  return got.ok() && same_request(got.value(), req);
}

static bool check_latest_schedule_overwrites_last() {
  FakeScheduler s;
  SyncRequest first{SyncConstraint::kAny, false};
  SyncRequest second{SyncConstraint::kUnmetered, true};
  if (s.schedule(first) != Status::kOk) return false;
  if (s.schedule(second) != Status::kOk) return false;
  auto got = s.last();
  return got.ok() && same_request(got.value(), second);
}

static bool check_seam_binds_through_base_reference() {
  // The plan names SyncScheduler as the seam: production code schedules
  // through SyncScheduler&, tests inject FakeScheduler. This proves the
  // IS-A binding without a live backend.
  FakeScheduler fake;
  SyncScheduler& seam = fake;
  SyncRequest req{SyncConstraint::kUnmetered, false};
  if (seam.schedule(req) != Status::kOk) return false;
  auto got = fake.last();
  return got.ok() && same_request(got.value(), req);
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
  RUN_CASE(check_last_on_empty_returns_not_found);
  RUN_CASE(check_schedule_any_is_recorded);
  RUN_CASE(check_schedule_unmetered_with_doze_exemption_is_recorded);
  RUN_CASE(check_schedule_idle_under_doze_defers_without_live_process);
  RUN_CASE(check_schedule_idle_without_exemption_is_recorded);
  RUN_CASE(check_latest_schedule_overwrites_last);
  RUN_CASE(check_seam_binds_through_base_reference);
  if (failures > 0) {
    std::printf("background_sync_test: %d failing\n", failures);
    return 1;
  }
  std::printf("background_sync_test: all passing\n");
  return 0;
}
