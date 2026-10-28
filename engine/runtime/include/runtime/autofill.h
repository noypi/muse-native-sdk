#pragma once

#include <string>

#include "runtime/gap_buffer.h"

// Autofill dataset routing (FR-TEXT-013 / AC-TEXT-033). Hermetic: dataset
// values are applied verbatim into the bound GapBuffer at append-at-end;
// no platform autofill service, no network, no persistence.
enum class AutofillHint { kUsername, kPassword, kEmail, kPhone, kNone };

struct AutofillField {
  int field_id = 0;
  AutofillHint hint = AutofillHint::kNone;
  std::string value;
};

inline void set_autofill_hint(AutofillField& f, AutofillHint h) noexcept {
  f.hint = h;
}

inline bool apply_autofill_dataset(GapBuffer& buf,
                                   const AutofillField& f) noexcept {
  try {
    switch (f.hint) {
      case AutofillHint::kUsername:
      case AutofillHint::kPassword:
      case AutofillHint::kEmail:
      case AutofillHint::kPhone:
        break;
      case AutofillHint::kNone:
        return false;
    }
    if (!f.value.empty()) {
      buf.insert(buf.byte_size(), f.value);
    }
    return true;
  } catch (...) {
    return false;
  }
}
