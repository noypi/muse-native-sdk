// T2 contract tests — FR-RUNTIME-004 / AC-RUNTIME-009 (TC-RUNTIME-009
// hermetic half; device-lab ime-smoke stays external).
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Binds to the T2 `Interface:` line; struct/field names below are the test's
// proposal and the T2 contract records the reconciliation rule (update call
// sites, not expectations) if the coder names them differently.
//
// ASSUMPTION (observation channel): `copy(const TextField&)` returns the
// selected range [a, b); selecting the whole text and copying observes the
// committed text. `TextField` is default-constructible and empty.
// ASSUMPTION (semantics): `commit_text` inserts at the caret/selection;
// `paste` replaces the selection; `apply_composition` stages marked text
// without committing it; out-of-range selection/caret clamps instead of
// throwing.
#include <cstdio>
#include <string>

#include "runtime/text_field.h"

namespace {

std::string committed_text(runtime::TextField& f) {
  runtime::set_selection(f, 0, static_cast<std::size_t>(-1));
  return runtime::copy(f);
}

bool check_commit_then_select_all_copies_text() {
  runtime::TextField f;
  runtime::commit_text(f, "hello");
  return committed_text(f) == "hello";
}

bool check_copy_empty_field_yields_empty_string() {
  runtime::TextField f;
  bool threw = false;
  std::string out = "sentinel";
  try {
    out = runtime::copy(f);
  } catch (...) {
    threw = true;
  }
  return !threw && out.empty();
}

bool check_selection_clamps_out_of_range() {
  runtime::TextField f;
  runtime::commit_text(f, "hi");
  bool threw = false;
  std::string out;
  try {
    runtime::set_selection(f, 100, 200);
    out = runtime::copy(f);
  } catch (...) {
    threw = true;
  }
  return !threw && out.empty();
}

bool check_reversed_selection_clamps_or_normalizes() {
  runtime::TextField f;
  runtime::commit_text(f, "hello");
  bool threw = false;
  std::string out;
  try {
    runtime::set_selection(f, 4, 1);
    out = runtime::copy(f);
  } catch (...) {
    threw = true;
  }
  // Either order yields the "ell" range or clamps to empty; it must not
  // throw and must not escape the committed text.
  return !threw && (out == "ell" || out.empty());
}

bool check_composition_stages_then_commit_confirms() {
  runtime::TextField f;
  runtime::apply_composition(f, "ni", 1);
  runtime::commit_text(f, "n");
  const std::string got = committed_text(f);
  return got == "n";
}

bool check_composition_caret_out_of_range_clamps() {
  runtime::TextField f;
  bool threw = false;
  try {
    runtime::apply_composition(f, "ab", 99);
    runtime::commit_text(f, "a");
  } catch (...) {
    threw = true;
  }
  return !threw && committed_text(f) == "a";
}

bool check_paste_replaces_selection() {
  runtime::TextField f;
  runtime::commit_text(f, "hello");
  runtime::set_selection(f, 1, 4);
  runtime::paste(f, "XY");
  return committed_text(f) == "hXYo";
}

bool check_paste_empty_selection_inserts() {
  runtime::TextField f;
  runtime::commit_text(f, "ac");
  runtime::set_selection(f, 1, 1);
  runtime::paste(f, "b");
  return committed_text(f) == "abc";
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
  RUN_CASE(check_commit_then_select_all_copies_text);
  RUN_CASE(check_copy_empty_field_yields_empty_string);
  RUN_CASE(check_selection_clamps_out_of_range);
  RUN_CASE(check_reversed_selection_clamps_or_normalizes);
  RUN_CASE(check_composition_stages_then_commit_confirms);
  RUN_CASE(check_composition_caret_out_of_range_clamps);
  RUN_CASE(check_paste_replaces_selection);
  RUN_CASE(check_paste_empty_selection_inserts);
  if (failures > 0) {
    std::printf("text_editing_test: %d failing\n", failures);
    return 1;
  }
  std::printf("text_editing_test: all passing\n");
  return 0;
}
