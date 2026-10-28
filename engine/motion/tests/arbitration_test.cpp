// T4 contract tests — FR-MOTION-008 / AC-MOTION-020 (TC-MOTION-020).
// Hermetic: C++17 + motion headers only; no network, no env/config reads,
// no filesystem, no live services. Deterministic: no wall clock, no
// randomness; determinism cases run the same input repeatedly and shuffled.
// Binds to the T4 `Interface:` line (reproduced verbatim in unit-tests/T4.md).
//
// ASSUMPTION (names): the plan names `motion::Claim{NodeId id; int priority;
// float startMs;}` and `motion::arbitrate(std::vector<motion::Claim>)` with
// `Winner{none:true}` on empty, but leaves the `Winner` shape open. These
// tests use `struct Winner { NodeId id; bool none = false; }` in `motion::`
// with the winning claim's id in `Winner::id`. If the coder names fields
// differently, update call sites — not expectations — per reconciliation.
//
// Documented rule under test (also pinned in arbitration.h header comment):
// LAST-WRITE-WINS on tie of (priority desc, startMs desc, id asc).
#include <algorithm>
#include <cstdio>
#include <vector>

#include "motion/arbitration.h"
#include "motion/graph.h"

namespace {

motion::NodeId nid(std::uint32_t v) {
  motion::NodeId id;
  id.value = v;
  return id;
}

motion::Claim claim(std::uint32_t id, int priority, float startMs) {
  motion::Claim c;
  c.id = nid(id);
  c.priority = priority;
  c.startMs = startMs;
  return c;
}

// Empty claims returns Winner{none:true}.
bool check_empty_returns_none() {
  motion::Winner w = motion::arbitrate({});
  return w.none == true;
}

// Single claim wins.
bool check_single_claim_wins() {
  motion::Winner w = motion::arbitrate({claim(7, 1, 100.0f)});
  return w.none == false && w.id == nid(7);
}

// Higher priority wins regardless of earlier startMs.
bool check_higher_priority_wins() {
  std::vector<motion::Claim> claims = {claim(1, 10, 50.0f),
                                       claim(2, 1, 900.0f)};
  motion::Winner w = motion::arbitrate(claims);
  return w.none == false && w.id == nid(1);
}

// Equal priority: later startMs wins (LAST-WRITE-WINS).
bool check_later_start_wins_on_priority_tie() {
  std::vector<motion::Claim> claims = {claim(1, 5, 100.0f),
                                       claim(2, 5, 300.0f),
                                       claim(3, 5, 200.0f)};
  motion::Winner w = motion::arbitrate(claims);
  return w.none == false && w.id == nid(2);
}

// Full tie (priority + startMs): smaller id wins (id asc).
bool check_smaller_id_wins_on_full_tie() {
  std::vector<motion::Claim> claims = {claim(9, 3, 100.0f),
                                       claim(4, 3, 100.0f),
                                       claim(6, 3, 100.0f)};
  motion::Winner w = motion::arbitrate(claims);
  return w.none == false && w.id == nid(4);
}

// Overlapping claims resolve to the documented winner: highest priority,
// then latest start, then lowest id.
bool check_overlapping_claims_documented_winner() {
  std::vector<motion::Claim> claims = {
      claim(10, 1, 1000.0f),  // low priority, latest start: loses
      claim(20, 5, 100.0f),   // high priority, early: contender
      claim(30, 5, 400.0f),   // high priority, later: winner
  };
  motion::Winner w = motion::arbitrate(claims);
  return w.none == false && w.id == nid(30);
}

// Repeated runs are identical (deterministic across runs/machines).
bool check_repeated_runs_identical() {
  std::vector<motion::Claim> claims = {claim(3, 2, 50.0f),
                                       claim(1, 2, 50.0f),
                                       claim(2, 4, 10.0f)};
  motion::Winner first = motion::arbitrate(claims);
  for (int i = 0; i < 8; ++i) {
    motion::Winner again = motion::arbitrate(claims);
    if (again.none != first.none || !(again.id == first.id)) return false;
  }
  return first.none == false && first.id == nid(2);
}

// Input order does not affect the winner (deterministic, order-independent).
bool check_input_order_independent() {
  std::vector<motion::Claim> a = {claim(1, 2, 10.0f), claim(2, 2, 20.0f),
                                  claim(3, 1, 99.0f)};
  std::vector<motion::Claim> b = a;
  std::reverse(b.begin(), b.end());
  motion::Winner wa = motion::arbitrate(a);
  motion::Winner wb = motion::arbitrate(b);
  return wa.none == false && wb.none == false && wa.id == wb.id &&
         wa.id == nid(2);
}

// Negative priorities still order by desc (less-negative wins).
bool check_negative_priority_ordering() {
  std::vector<motion::Claim> claims = {claim(1, -5, 10.0f),
                                       claim(2, -1, 10.0f)};
  motion::Winner w = motion::arbitrate(claims);
  return w.none == false && w.id == nid(2);
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
  RUN_CASE(check_empty_returns_none);
  RUN_CASE(check_single_claim_wins);
  RUN_CASE(check_higher_priority_wins);
  RUN_CASE(check_later_start_wins_on_priority_tie);
  RUN_CASE(check_smaller_id_wins_on_full_tie);
  RUN_CASE(check_overlapping_claims_documented_winner);
  RUN_CASE(check_repeated_runs_identical);
  RUN_CASE(check_input_order_independent);
  RUN_CASE(check_negative_priority_ordering);
  if (failures > 0) {
    std::printf("arbitration_test: %d failing\n", failures);
    return 1;
  }
  std::printf("arbitration_test: all passing\n");
  return 0;
}
