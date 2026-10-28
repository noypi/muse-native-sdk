// T3 — FR-TEXT-014 / AC-TEXT-039: palm rejection before stroke/gesture emission.
// Hermetic harness: plain C++17, no test framework, no device, no gesture bus,
// no downloads. Cases drive only the plan Interface (reproduced verbatim
// below) with synthetic inline contact events. Shape mirrors
// engine/text/tests/stylus_samples_test.cpp: static bool check_*() cases +
// main, return 0 all-green / non-zero + failing name.
//
// Classifier only: the tests assert verdicts. That rejected contacts emit
// neither stroke points nor scroll/gesture events (emission suppression) is
// observed at the pipeline/acceptance layer, not in this unit — see contract.
#include <cstdio>
#include <type_traits>

#include "../palm_rejector.h"

// Plan Interface lines (verbatim):
// `RejectVerdict Classify(const ContactEvent& e, const StylusActiveState& s)`
// → `enum class RejectVerdict{ACCEPT_STYLUS, REJECT_PALM}`; rejected contacts
// emit neither stroke points nor scroll/gesture events:
// `none — classifier only, emission suppression is the observable`.
//
// ASSUMPTION (structural, for the coder — see contract T3.md): `Classify` is
// a pure free function in `muse::text::ink`; `ContactEvent{ palm: bool }`
// (default non-palm touch) and `StylusActiveState{ stylusActive: bool }`
// (default inactive) are plain value structs with exactly the fields the
// verdict needs. Verdict depends only on (e, s): no hidden state, no I/O.

namespace ink = muse::text::ink;

static ink::ContactEvent make_contact(bool palm) {
  ink::ContactEvent e{};
  e.palm = palm;
  return e;
}

static ink::StylusActiveState make_state(bool stylus_active) {
  ink::StylusActiveState s{};
  s.stylusActive = stylus_active;
  return s;
}

static bool check_palm_while_stylus_active_rejected() {
  return ink::Classify(make_contact(/*palm=*/true),
                       make_state(/*stylus_active=*/true)) ==
         ink::RejectVerdict::REJECT_PALM;
}

static bool check_stylus_contact_while_active_accepted() {
  return ink::Classify(make_contact(/*palm=*/false),
                       make_state(/*stylus_active=*/true)) ==
         ink::RejectVerdict::ACCEPT_STYLUS;
}

static bool check_stylus_down_while_inactive_accepted() {
  // Pen-down with no active stylus is the activation itself, not a palm.
  return ink::Classify(make_contact(/*palm=*/false),
                       make_state(/*stylus_active=*/false)) ==
         ink::RejectVerdict::ACCEPT_STYLUS;
}

static bool check_palm_while_inactive_rejected() {
  // ASSUMPTION: a palm contact never emits strokes even with no active
  // stylus (AC-TEXT-039: palm yields no extra stroke or scroll).
  return ink::Classify(make_contact(/*palm=*/true),
                       make_state(/*stylus_active=*/false)) ==
         ink::RejectVerdict::REJECT_PALM;
}

static bool check_default_event_is_non_palm_touch() {
  // Boundary: a default-constructed event with an inactive stylus is an
  // ordinary touch, accepted — the classifier must not reject blindly.
  return ink::Classify(ink::ContactEvent{}, ink::StylusActiveState{}) ==
         ink::RejectVerdict::ACCEPT_STYLUS;
}

static bool check_classifier_is_pure_and_stateless() {
  // Same inputs always give the same verdict, back to back, and the inputs
  // are not mutated (const-ref observable behavior, no hidden latch that a
  // later palm could depend on).
  const ink::ContactEvent palm = make_contact(true);
  const ink::ContactEvent touch = make_contact(false);
  const ink::StylusActiveState active = make_state(true);
  for (int i = 0; i < 3; ++i) {
    if (ink::Classify(palm, active) != ink::RejectVerdict::REJECT_PALM) {
      return false;
    }
    if (ink::Classify(touch, active) != ink::RejectVerdict::ACCEPT_STYLUS) {
      return false;
    }
  }
  return palm.palm && active.stylusActive;
}

static bool check_alternating_palm_stylus_classified_independently() {
  // Simulated palm contact during an active pen stroke: each contact is
  // judged on its own flags — no order dependence, no shared mutable state.
  const ink::StylusActiveState active = make_state(true);
  const ink::RejectVerdict expected[] = {
      ink::RejectVerdict::ACCEPT_STYLUS, ink::RejectVerdict::REJECT_PALM,
      ink::RejectVerdict::ACCEPT_STYLUS, ink::RejectVerdict::REJECT_PALM,
      ink::RejectVerdict::ACCEPT_STYLUS};
  const bool palms[] = {false, true, false, true, false};
  for (int i = 0; i < 5; ++i) {
    if (ink::Classify(make_contact(palms[i]), active) != expected[i]) {
      return false;
    }
  }
  return true;
}

static bool check_verdict_enum_shape() {
  // The plan's verdict vocabulary, exactly: two enumerators, distinct values.
  constexpr ink::RejectVerdict accept = ink::RejectVerdict::ACCEPT_STYLUS;
  constexpr ink::RejectVerdict reject = ink::RejectVerdict::REJECT_PALM;
  static_assert(accept != reject);
  static_assert(std::is_same_v<
                decltype(&ink::Classify),
                ink::RejectVerdict (*)(const ink::ContactEvent&,
                                       const ink::StylusActiveState&)>);
  return accept == ink::RejectVerdict::ACCEPT_STYLUS &&
         reject == ink::RejectVerdict::REJECT_PALM;
}

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_palm_while_stylus_active_rejected);
  RUN_CASE(check_stylus_contact_while_active_accepted);
  RUN_CASE(check_stylus_down_while_inactive_accepted);
  RUN_CASE(check_palm_while_inactive_rejected);
  RUN_CASE(check_default_event_is_non_palm_touch);
  RUN_CASE(check_classifier_is_pure_and_stateless);
  RUN_CASE(check_alternating_palm_stylus_classified_independently);
  RUN_CASE(check_verdict_enum_shape);
  if (failures > 0) {
    std::printf("palm_rejection_test: %d failing\n", failures);
    return 1;
  }
  std::printf("palm_rejection_test: all passing\n");
  return 0;
}
