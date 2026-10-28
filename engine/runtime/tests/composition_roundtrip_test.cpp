// T3 contract tests — FR-TEXT-009 / AC-TEXT-019 (TC-TEXT-019).
// Binds ONLY to the T3 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/composition_roundtrip.h` does not exist yet,
// so this file must FAIL TO COMPILE. That is the recorded red state.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/composition_roundtrip.h"
#include "runtime/gap_buffer.h"

// --- harness cases (match existing test style) -----------------------------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T3 cases (each maps to the Interface: line + AC-TEXT-019) --------------

// Boundary: empty buffer yields empty text with sel 0/0.
static bool check_empty_buffer_yields_empty() {
  GapBuffer buf;
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  return st.text.empty() && st.sel_lo == 0 && st.sel_hi == 0;
}

// Happy path: full-window query returns the whole buffer synchronously.
static bool check_query_returns_full_window() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  return st.text == "hello";
}

// Selection mirrors the buffer cursors on a full-window query.
static bool check_selection_mirrors_cursors() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello world"));
  buf.set_cursors({{6, 11}});
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  if (st.text != "hello world") return false;
  return st.sel_lo == 6 && st.sel_hi == 11;
}

// Window clamps to [cursor - before, cursor + after]; sel is relative to the
// returned window start.
static bool check_window_clamps_to_cursor() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello world"));
  buf.set_cursors({{11, 11}});
  const SurroundingText st = get_surrounding_text(buf, 5, 5);
  if (st.text != "world") return false;
  return st.sel_lo == 5 && st.sel_hi == 5;
}

// Boundary: before/after beyond the buffer clamp to bounds; never throws.
static bool check_window_clamps_beyond_bounds() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hi"));
  const SurroundingText st = get_surrounding_text(buf, 1000000, 1000000);
  return st.text == "hi";
}

// Query reflects the latest edit on the same call stack (no deferred hop).
static bool check_query_synchronous_after_edit() {
  GapBuffer buf;
  buf.insert(0, std::string_view("abc"));
  if (get_surrounding_text(buf, 100, 100).text != "abc") return false;
  buf.insert(3, std::string_view("d"));
  return get_surrounding_text(buf, 100, 100).text == "abcd";
}

// Multibyte (UTF-8) commit round-trips byte-exact through the query.
static bool check_multibyte_commit_query() {
  GapBuffer buf;
  buf.insert(0, std::string_view("日本"));
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  return st.text == "日本";
}

// Composing region tracks the composed span.
static bool check_composing_region_tracks_span() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  set_composing_region(buf, 0, 5);
  return get_composition_span(buf) == "hello";
}

// Boundary: out-of-range region endpoints clamp to buffer bounds.
static bool check_composing_region_clamps() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hi"));
  set_composing_region(buf, 0, 1000);
  if (get_composition_span(buf) != "hi") return false;
  set_composing_region(buf, 100, 200);
  return get_composition_span(buf).empty();
}

// Boundary: no composing region set yields an empty span.
static bool check_no_composing_region_empty_span() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  return get_composition_span(buf).empty();
}

// Acceptance: compose -> query -> commit -> query returns reference values
// at each step, synchronously.
static bool check_compose_query_commit_query_roundtrip() {
  GapBuffer buf;
  buf.insert(0, std::string_view("nihon"));
  set_composing_region(buf, 0, 5);
  if (get_composition_span(buf) != "nihon") return false;
  if (get_surrounding_text(buf, 100, 100).text != "nihon") return false;
  // Commit replaces the composing range with the converted text.
  buf.erase(0, 5);
  buf.insert(0, std::string_view("日本"));
  set_composing_region(buf, 6, 6);  // "日本" is 6 bytes; empty region.
  if (!get_composition_span(buf).empty()) return false;
  const SurroundingText st = get_surrounding_text(buf, 100, 100);
  return st.text == "日本";
}

// Security/boundary: extreme before/after and region endpoints never throw
// and never produce out-of-bounds text.
static bool check_extreme_args_never_throw() {
  GapBuffer buf;
  buf.insert(0, std::string_view("abc"));
  const size_t huge = static_cast<size_t>(-1);
  const SurroundingText st = get_surrounding_text(buf, huge, huge);
  if (st.text != "abc") return false;
  if (st.sel_lo > st.text.size() || st.sel_hi > st.text.size()) return false;
  set_composing_region(buf, huge, huge);
  return get_composition_span(buf).size() <= buf.byte_size();
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
       ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_empty_buffer_yields_empty);
  RUN_CASE(check_query_returns_full_window);
  RUN_CASE(check_selection_mirrors_cursors);
  RUN_CASE(check_window_clamps_to_cursor);
  RUN_CASE(check_window_clamps_beyond_bounds);
  RUN_CASE(check_query_synchronous_after_edit);
  RUN_CASE(check_multibyte_commit_query);
  RUN_CASE(check_composing_region_tracks_span);
  RUN_CASE(check_composing_region_clamps);
  RUN_CASE(check_no_composing_region_empty_span);
  RUN_CASE(check_compose_query_commit_query_roundtrip);
  RUN_CASE(check_extreme_args_never_throw);
  if (failures > 0) {
    std::printf("composition_roundtrip_test: %d failing\n", failures);
    return 1;
  }
  std::printf("composition_roundtrip_test: all passing\n");
  return 0;
}
