// T1 contract tests — FR-RUNTIME-004 / AC-RUNTIME-008 (TC-RUNTIME-008).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds to the T1 `Interface:` line; field/type names below are the test's
// proposal and the T1 contract records the reconciliation rule (update call
// sites, not expectations) if the coder names them differently.
//
// ASSUMPTION (types): `runtime::BundledFont` is default-constructible;
// `runtime::TextMetrics` is an aggregate with `float ascent, descent,
// advance`; `measure_text(const BundledFont&, std::string_view)` returns by
// value. Floats are exactly representable; no epsilon is used.
// ASSUMPTION (metrics): the bundled font gives positive ascent and positive
// advance for ASCII "Hello"; empty runs advance 0; advance is monotonic with
// run length for ASCII.
#include <cstdio>
#include <string_view>

#include "runtime/text.h"

namespace {

bool check_ascii_run_has_positive_advance() {
  runtime::BundledFont font;
  const runtime::TextMetrics m = runtime::measure_text(font, "Hello");
  return m.advance > 0.0f && m.ascent > 0.0f && m.descent >= 0.0f;
}

bool check_empty_run_has_zero_advance() {
  runtime::BundledFont font;
  bool threw = false;
  runtime::TextMetrics m{0.0f, 0.0f, 0.0f};
  try {
    m = runtime::measure_text(font, std::string_view());
  } catch (...) {
    threw = true;
  }
  return !threw && m.advance == 0.0f;
}

bool check_unknown_glyph_returns_zero_advance_without_throwing() {
  runtime::BundledFont font;
  bool threw = false;
  runtime::TextMetrics m{1.0f, 1.0f, 1.0f};
  try {
    // U+E000 private-use; bundled gallery font carries no such glyph.
    m = runtime::measure_text(font, "\xEE\x80\x80");
  } catch (...) {
    threw = true;
  }
  return !threw && m.advance == 0.0f;
}

bool check_metrics_are_deterministic() {
  runtime::BundledFont font;
  const runtime::TextMetrics a = runtime::measure_text(font, "Hello");
  const runtime::TextMetrics b = runtime::measure_text(font, "Hello");
  return a.ascent == b.ascent && a.descent == b.descent &&
         a.advance == b.advance;
}

bool check_longer_run_advances_further() {
  runtime::BundledFont font;
  const runtime::TextMetrics pre = runtime::measure_text(font, "Hel");
  const runtime::TextMetrics full = runtime::measure_text(font, "Hello");
  return full.advance >= pre.advance && full.advance > 0.0f;
}

bool check_ascent_descent_stable_across_runs() {
  runtime::BundledFont font;
  const runtime::TextMetrics a = runtime::measure_text(font, "Hi");
  const runtime::TextMetrics b = runtime::measure_text(font, "Hello world");
  return a.ascent == b.ascent && a.descent == b.descent;
}

bool check_mixed_known_unknown_never_throws() {
  runtime::BundledFont font;
  bool threw = false;
  try {
    const runtime::TextMetrics m =
        runtime::measure_text(font, "Hi\xEE\x80\x80!");
    (void)m.advance;
  } catch (...) {
    threw = true;
  }
  return !threw;
}

}  // namespace

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
  RUN_CASE(check_ascii_run_has_positive_advance);
  RUN_CASE(check_empty_run_has_zero_advance);
  RUN_CASE(check_unknown_glyph_returns_zero_advance_without_throwing);
  RUN_CASE(check_metrics_are_deterministic);
  RUN_CASE(check_longer_run_advances_further);
  RUN_CASE(check_ascent_descent_stable_across_runs);
  RUN_CASE(check_mixed_known_unknown_never_throws);
  if (failures > 0) {
    std::printf("text_metrics_test: %d failing\n", failures);
    return 1;
  }
  std::printf("text_metrics_test: all passing\n");
  return 0;
}
