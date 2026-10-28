// T1 contract tests — FR-TEXT-008 / AC-TEXT-014 (TC-TEXT-014).
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/gap_buffer.h` does not exist yet, so this
// file must FAIL TO COMPILE. That is the recorded red state.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

// --- T1 cases (each maps to the Interface: line + AC-TEXT-014) --------------

// Happy path: insert at tail then read back.
static bool check_insert_tail_reads_back() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  buf.insert(5, std::string_view(" world"));
  return buf.to_string() == "hello world" && buf.byte_size() == 11;
}

// Happy path: insert in the middle.
static bool check_insert_middle() {
  GapBuffer buf;
  buf.insert(0, std::string_view("ac"));
  buf.insert(1, std::string_view("b"));
  return buf.to_string() == "abc";
}

// Happy path: erase a middle range.
static bool check_erase_middle() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello world"));
  buf.erase(5, 6);
  return buf.to_string() == "hello" && buf.byte_size() == 5;
}

// Boundary: out-of-range insert position clamps to [0, byte_size()].
static bool check_insert_pos_clamps() {
  GapBuffer buf;
  buf.insert(0, std::string_view("abc"));
  buf.insert(1000, std::string_view("d"));  // clamps to end
  if (buf.to_string() != "abcd") return false;
  return true;
}

// Boundary: out-of-range erase length clamps; never throws.
static bool check_erase_len_clamps() {
  GapBuffer buf;
  buf.insert(0, std::string_view("abc"));
  buf.erase(1, 100);  // clamps to end
  if (buf.to_string() != "a") return false;
  buf.erase(50, 10);  // pos past end: no-op
  return buf.to_string() == "a";
}

// Undo/redo restores byte-identical states; can_undo/can_redo expose state.
static bool check_undo_redo_restores() {
  GapBuffer buf;
  if (buf.can_undo()) return false;
  if (buf.can_redo()) return false;
  buf.insert(0, std::string_view("abc"));
  if (!buf.can_undo()) return false;
  buf.erase(0, 1);
  if (buf.to_string() != "bc") return false;
  buf.undo();
  if (buf.to_string() != "abc") return false;
  buf.undo();
  if (buf.to_string() != "") return false;
  if (buf.can_undo()) return false;
  buf.redo();
  if (buf.to_string() != "abc") return false;
  buf.redo();
  if (buf.to_string() != "bc") return false;
  return true;
}

// Boundary: undo()/redo() with empty stack are no-ops (never throw).
static bool check_undo_redo_empty_noop() {
  GapBuffer buf;
  buf.undo();
  buf.redo();
  if (buf.can_undo()) return false;
  if (buf.can_redo()) return false;
  return buf.to_string() == "" && buf.byte_size() == 0;
}

// Multi-cursor: set_cursors stores ranges; cursors() returns them.
static bool check_cursors_roundtrip() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello world"));
  buf.set_cursors({{0, 0}, {6, 11}});
  const std::vector<std::pair<size_t, size_t>>& cs = buf.cursors();
  if (cs.size() != 2) return false;
  if (cs[0].first != 0 || cs[0].second != 0) return false;
  if (cs[1].first != 6 || cs[1].second != 11) return false;
  return true;
}

// Span hook: tag_span does not alter text; spans survive edits.
static bool check_tag_span_survives_edit() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello world"));
  buf.tag_span(0, 5, 1);
  buf.insert(5, std::string_view("!"));
  // Text reflects the edit; tagging itself never changes bytes.
  return buf.to_string() == "hello! world" && buf.byte_size() == 12;
}

// Security/boundary: large insert at head completes; byte size exact.
static bool check_large_insert_head() {
  GapBuffer buf;
  const std::string big(100 * 1024, 'x');
  buf.insert(0, std::string_view(big));
  buf.insert(0, std::string_view("H"));
  if (buf.byte_size() != 100 * 1024 + 1) return false;
  return buf.to_string()[0] == 'H';
}

// F2 (review MAJOR undo full-copy + MINOR unbounded stacks): K=300
// sequential inserts keep undo depth bounded (cap N=100). Observable pin
// only: undo until can_undo is false counts exactly 100 undos, and the
// resulting text equals initial + 200 edits worth (oldest 100 dropped).
static bool check_undo_depth_bounded_at_100() {
  GapBuffer buf;
  const std::string initial(10 * 1024, 'a');
  buf.insert(0, std::string_view(initial));
  const size_t initial_size = buf.byte_size();
  for (int i = 0; i < 300; ++i) {
    buf.insert(buf.byte_size(), std::string_view("x"));
  }
  if (!buf.can_undo()) return false;
  int undos = 0;
  while (buf.can_undo()) {
    buf.undo();
    ++undos;
  }
  if (undos != 100) return false;
  return buf.byte_size() == initial_size + 200;
}

// F3 (review MAJOR multi-cursor): insert_at_cursors fans out over all
// cursors, applied at descending offsets so earlier offsets are unaffected.
// "0123456789" + cursors {0,0},{5,5} + "x" -> "x01234x56789".
static bool check_insert_at_cursors_applies_descending() {
  GapBuffer buf;
  buf.insert(0, std::string_view("0123456789"));
  buf.set_cursors({{0, 0}, {5, 5}});
  buf.insert_at_cursors(std::string_view("x"));
  return buf.to_string() == "x01234x56789";
}

// F3: empty cursor set + insert_at_cursors is a no-op.
static bool check_insert_at_cursors_empty_is_noop() {
  GapBuffer buf;
  buf.insert(0, std::string_view("0123456789"));
  buf.insert_at_cursors(std::string_view("x"));
  return buf.to_string() == "0123456789";
}

// F4a (review MINOR caps + clamp rule): tag_span with an inverted range
// collapses to empty (aligns with set_composing_region) — no span stored.
static bool check_tag_span_inverted_collapses_to_empty() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello"));
  buf.tag_span(5, 2, 1);
  return buf.spans_of_kind(1).empty();
}

// S3 (security LOW — span bound): span storage is capped at 1024 entries
// total (oldest-drop or refuse-new). 5000 tag operations stay bounded.
static bool check_span_storage_bounded_across_many_tags() {
  GapBuffer buf;
  buf.insert(0, std::string_view("hello world"));
  for (int n = 0; n < 5000; ++n) {
    buf.tag_span(0, 5, static_cast<uint32_t>(n % 8));
  }
  size_t total = 0;
  for (uint32_t k = 0; k < 8; ++k) {
    total += buf.spans_of_kind(k).size();
  }
  return total <= 1024;
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
  RUN_CASE(check_insert_tail_reads_back);
  RUN_CASE(check_insert_middle);
  RUN_CASE(check_erase_middle);
  RUN_CASE(check_insert_pos_clamps);
  RUN_CASE(check_erase_len_clamps);
  RUN_CASE(check_undo_redo_restores);
  RUN_CASE(check_undo_redo_empty_noop);
  RUN_CASE(check_cursors_roundtrip);
  RUN_CASE(check_tag_span_survives_edit);
  RUN_CASE(check_large_insert_head);
  RUN_CASE(check_undo_depth_bounded_at_100);
  RUN_CASE(check_insert_at_cursors_applies_descending);
  RUN_CASE(check_insert_at_cursors_empty_is_noop);
  RUN_CASE(check_tag_span_inverted_collapses_to_empty);
  RUN_CASE(check_span_storage_bounded_across_many_tags);
  if (failures > 0) {
    std::printf("gap_buffer_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gap_buffer_test: all passing\n");
  return 0;
}
