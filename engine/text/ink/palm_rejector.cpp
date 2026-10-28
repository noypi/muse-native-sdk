// FR-TEXT-014 / AC-TEXT-039: palm rejection classifier implementation.
// A palm contact never becomes a stroke or gesture, with or without an
// active stylus; every other contact is accepted untouched.

#include "palm_rejector.h"

namespace muse::text::ink {

RejectVerdict Classify(const ContactEvent& e, const StylusActiveState& s) {
  (void)s;
  if (e.palm) return RejectVerdict::REJECT_PALM;
  return RejectVerdict::ACCEPT_STYLUS;
}

}  // namespace muse::text::ink
