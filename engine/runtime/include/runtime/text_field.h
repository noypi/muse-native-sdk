#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace runtime {

// IME editing model (FR-RUNTIME-004 / AC-RUNTIME-009). Hermetic: the
// platform IME feeds composition/commit through these functions; caret and
// selection render state derives from the model. No I/O, never throws.
struct TextField {
  std::string text;
  std::size_t sel_a = 0;
  std::size_t sel_b = 0;
  std::string composition;
  std::size_t composition_caret = 0;
};

namespace detail {

inline std::size_t clamp_to_text(const TextField& f, std::size_t i) {
  return (std::min)(i, f.text.size());
}

inline void replace_selection(TextField& f, std::string_view s) {
  const std::size_t lo =
      (std::min)(detail::clamp_to_text(f, f.sel_a), detail::clamp_to_text(f, f.sel_b));
  // Note: sel_a/sel_b are stored normalized (see set_selection), but
  // clamp-then-min keeps this helper robust regardless of ordering.
  const std::size_t hi =
      (std::max)(detail::clamp_to_text(f, f.sel_a), detail::clamp_to_text(f, f.sel_b));
  f.text.replace(lo, hi - lo, s.data(), s.size());
  const std::size_t caret = lo + s.size();
  f.sel_a = caret;
  f.sel_b = caret;
  f.composition.clear();
  f.composition_caret = 0;
}

}  // namespace detail

// Stage marked (pre-edit) text without committing it. The caret offset into
// the marked text clamps to [0, marked.size()]; committed text is untouched,
// so a later commit_text replaces the staged composition with confirmed text.
inline void apply_composition(TextField& f, std::string_view marked,
                              int caret_in_marked) {
  f.composition.assign(marked.data(), marked.size());
  if (caret_in_marked < 0) {
    f.composition_caret = 0;
  } else {
    f.composition_caret =
        (std::min)(static_cast<std::size_t>(caret_in_marked), marked.size());
  }
}

// Confirm text: discard any staged composition, then replace the current
// selection (or insert at the caret when the selection is empty).
inline void commit_text(TextField& f, std::string_view s) {
  f.composition.clear();
  f.composition_caret = 0;
  detail::replace_selection(f, s);
}

// Out-of-range indices clamp to [0, text.size()]; reversed endpoints are
// normalized so copy/paste observe the enclosed range. Never throws.
inline void set_selection(TextField& f, std::size_t a, std::size_t b) {
  const std::size_t ca = detail::clamp_to_text(f, a);
  const std::size_t cb = detail::clamp_to_text(f, b);
  if (ca <= cb) {
    f.sel_a = ca;
    f.sel_b = cb;
  } else {
    f.sel_a = cb;
    f.sel_b = ca;
  }
}

// Return the selected range [lo, hi); empty selection or empty field yields
// an empty string. Never throws.
inline std::string copy(const TextField& f) {
  const std::size_t ca = detail::clamp_to_text(f, f.sel_a);
  const std::size_t cb = detail::clamp_to_text(f, f.sel_b);
  const std::size_t lo = (std::min)(ca, cb);
  const std::size_t hi = (std::max)(ca, cb);
  return f.text.substr(lo, hi - lo);
}

// Replace the current selection with s (empty selection inserts at caret).
inline void paste(TextField& f, std::string_view s) {
  detail::replace_selection(f, s);
}

}  // namespace runtime
