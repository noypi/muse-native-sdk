#pragma once

// FR-TEXT-014 / AC-TEXT-039: palm rejection classifier for ink contacts.
// Pure C++ verdict over the FR-INPUT-003 dispatch contract: palm contacts
// are rejected before stroke/gesture emission, stylus contacts are
// accepted. Classifier only — emission suppression (rejected contacts
// yield neither stroke points nor scroll/gesture events) is observed at
// the pipeline/acceptance layer, not here. Pure free function, no hidden
// state, no I/O. Never throws.

namespace muse::text::ink {

// One input contact: whether the dispatch layer flagged it as palm
// (untrusted large contact) per FR-INPUT-003.
struct ContactEvent {
  bool palm = false;
};

// Whether a stylus stroke is currently active (pen-down seen).
struct StylusActiveState {
  bool stylusActive = false;
};

// Classification verdict: accepted contacts flow to stroke/gesture
// handling; rejected palm contacts emit nothing.
enum class RejectVerdict {
  ACCEPT_STYLUS = 0,
  REJECT_PALM = 1,
};

// Classifies one contact. Palm contacts always classify REJECT_PALM
// (even with no active stylus — a palm never emits strokes); all other
// contacts classify ACCEPT_STYLUS. Verdict depends only on (e, s);
// inputs are never mutated. Never throws.
RejectVerdict Classify(const ContactEvent& e, const StylusActiveState& s);

}  // namespace muse::text::ink
