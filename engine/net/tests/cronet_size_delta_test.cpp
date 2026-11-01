// Batch 3 contract tests — T7 (FR-NET-014 / AC-NET-028, TC-NET-028).
// Record the Cronet size-delta against the perf footprint estimates
// (Q-NET-001 gate). Method is MANUAL per the spec table; this file is the
// in-repo red marker that keeps the gate visible until a human files the
// record.
//
// Binds to the T7 `Interface:` line (plan.md, verbatim):
//   none — manual measurement record; produces
//   docs/build/cronet-size-delta.md with per-app delta vs NFR-BUILD-019
//   estimates
// There is no callable production surface: the "implementation" is the
// human-run measurement plus the docs record. Per the tester skill, an
// acceptance criterion that is not unit-testable as written is covered by
// the closest observable marker and the criterion is marked UNVERIFIED as
// automated (see unit-tests/T7.md); the manual procedure below is the
// TC-NET-028 Check.
//
// Each case prints its manual step and fails while the record is unfiled,
// so the red run reads as the open gate. A case turns green only when the
// tester verifies the corresponding record content in the same change set
// (never by weakening the assertion).
//
// Hermetic: C++17, libc only. No network, no env/config reads, no
// filesystem access, no live services, no secrets.
#include <cstdio>

namespace {

// Step 1 of TC-NET-028: build the reference binary with and without Cronet
// from the same commit.
bool check_both_binaries_built_from_same_commit() {
  std::printf(
      "MANUAL check_both_binaries_built_from_same_commit: build the "
      "reference binary WITH and WITHOUT Cronet from the same commit; "
      "Q-NET-001 is open so no record exists yet\n");
  return false;
}

// Step 2 of TC-NET-028: record the per-app size delta against the perf
// footprint estimates (NFR-BUILD-019 by reference; perf holds the numbers:
// base compressed APK 12-18 MB class per AC-PERFORMANCE-009).
bool check_per_app_delta_recorded_vs_perf_estimates() {
  std::printf(
      "MANUAL check_per_app_delta_recorded_vs_perf_estimates: record the "
      "per-app size delta against the NFR-BUILD-019 / perf footprint "
      "estimates in docs/build/cronet-size-delta.md; no numbers filed "
      "yet\n");
  return false;
}

// Step 3 of TC-NET-028: confirm the delta is filed before Cronet ships as
// the default in any template (blocks T6 default shipment while Q-NET-001
// is open).
bool check_delta_filed_before_default_ships() {
  std::printf(
      "MANUAL check_delta_filed_before_default_ships: reviewer confirms "
      "the record is filed before Cronet ships as the default in any "
      "template; gate open, default must not ship yet\n");
  return false;
}

}  // namespace

#define RUN_CASE(name)                   \
  do {                                   \
    if (!(name)()) {                     \
      std::printf("FAIL %s\n", #name);   \
      ++failures;                        \
    } else {                             \
      std::printf("PASS %s\n", #name);   \
    }                                    \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_both_binaries_built_from_same_commit);
  RUN_CASE(check_per_app_delta_recorded_vs_perf_estimates);
  RUN_CASE(check_delta_filed_before_default_ships);
  if (failures > 0) {
    std::printf("cronet_size_delta_test: %d failing (MANUAL gate open)\n",
                failures);
    return 1;
  }
  std::printf("cronet_size_delta_test: all passing\n");
  return 0;
}
