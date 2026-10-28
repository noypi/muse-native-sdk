// T3 (FR-TEXT-005 / AC-TEXT-008) + T4 (FR-TEXT-005 / AC-TEXT-008 code mode).
// Hermetic harness: plain C++17, no test framework, no device, no downloads.
// Cases drive only the public rich_buffer + code_mode interfaces.
// Shape mirrors engine/text/tests/nonlinear_scale_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

#include "muse/text/code_mode.h"
#include "muse/text/rich_buffer.h"

// Plan Interface lines (verbatim):
// T3: enum class SpanKind : uint32_t { Bold, Italic, Underline, Strike,
//   Color, Background, Link, Code, Ruby }; struct InlineObject { uint32_t id;
//   float widthPx, heightPx; float baselineOffsetPx; }; class RichBuffer {
//   public: void insert(size_t, std::string_view); void erase(size_t, size_t);
//   void setSpan(size_t lo, size_t hi, SpanKind, uint32_t payload);
//   void insertObject(size_t pos, const InlineObject&); bool undo();
//   bool redo(); std::string plainText() const; };
// T4: struct CodeOptions { bool softWrap; uint32_t tabWidth; bool lineNumbers; };
//   size_t ExpandTabs(const RichBuffer& buf, size_t lineStart,
//   uint32_t tabWidth); bool MatchBracket(const RichBuffer& buf, size_t pos,
//   size_t* matchPos); uint32_t LineNumberAt(const RichBuffer& buf, size_t pos);

namespace {

constexpr char kObj[] = "\xEF\xBF\xBC";  // U+FFFC UTF-8 degradation marker.

bool utf8HasFffc(const std::string& s) {
  return s.find(kObj) != std::string::npos;
}

muse::text::RichBuffer makeHello() {
  muse::text::RichBuffer b;
  b.insert(0, "Hello");
  return b;
}

}  // namespace

// ---- T3: insert / erase / plainText ----

static bool check_insert_then_plaintext_roundtrips() {
  muse::text::RichBuffer b;
  b.insert(0, "Hello");
  b.insert(5, ", world");
  if (b.plainText() != "Hello, world") return false;
  return true;
}

static bool check_insert_empty_is_noop() {
  muse::text::RichBuffer b = makeHello();
  b.insert(2, "");
  if (b.plainText() != "Hello") return false;
  return true;
}

static bool check_erase_removes_range() {
  muse::text::RichBuffer b = makeHello();
  b.erase(1, 3);  // remove "ell"
  if (b.plainText() != "Ho") return false;
  return true;
}

static bool check_erase_zero_length_is_noop() {
  muse::text::RichBuffer b = makeHello();
  b.erase(2, 0);
  if (b.plainText() != "Hello") return false;
  return true;
}

static bool check_setspan_does_not_change_plaintext() {
  muse::text::RichBuffer b = makeHello();
  b.setSpan(0, 5, muse::text::SpanKind::Bold, 0u);
  b.setSpan(0, 5, muse::text::SpanKind::Color, 0xFF0000FFu);
  b.setSpan(1, 3, muse::text::SpanKind::Ruby, 7u);
  if (b.plainText() != "Hello") return false;
  return true;
}

static bool check_all_span_kinds_accepted() {
  muse::text::RichBuffer b = makeHello();
  using K = muse::text::SpanKind;
  b.setSpan(0, 1, K::Bold, 0u);
  b.setSpan(0, 1, K::Italic, 0u);
  b.setSpan(0, 1, K::Underline, 0u);
  b.setSpan(0, 1, K::Strike, 0u);
  b.setSpan(0, 1, K::Color, 0xFFFFFFFFu);
  b.setSpan(0, 1, K::Background, 0u);
  b.setSpan(0, 1, K::Link, 42u);
  b.setSpan(0, 1, K::Code, 0u);
  b.setSpan(0, 1, K::Ruby, 1u);
  if (b.plainText() != "Hello") return false;
  // ASSUMPTION: SpanKind underlying values are stable 0..8 in declaration
  // order (Bold=0 .. Ruby=8); coder keeps the plan order.
  if (static_cast<uint32_t>(K::Bold) != 0u) return false;
  if (static_cast<uint32_t>(K::Ruby) != 8u) return false;
  return true;
}

static bool check_object_degrades_to_fffc() {
  muse::text::RichBuffer b = makeHello();
  muse::text::InlineObject o{7u, 16.0f, 16.0f, 0.0f};
  b.insertObject(5, o);
  const std::string t = b.plainText();
  if (t != std::string("Hello") + kObj) return false;
  if (!utf8HasFffc(t)) return false;
  return true;
}

static bool check_object_mid_text_degrades_inline() {
  muse::text::RichBuffer b = makeHello();
  muse::text::InlineObject o{1u, 10.0f, 20.0f, -2.0f};
  b.insertObject(2, o);
  if (b.plainText() != std::string("He") + kObj + "llo") return false;
  return true;
}

static bool check_edit_session_undo_redo_restores_text() {
  muse::text::RichBuffer b = makeHello();
  b.setSpan(0, 5, muse::text::SpanKind::Bold, 0u);
  muse::text::InlineObject o{3u, 12.0f, 12.0f, 0.0f};
  b.insertObject(5, o);
  b.insert(0, "Say ");
  const std::string before = b.plainText();
  if (before != std::string("Say Hello") + kObj) return false;
  // Undo everything back to empty.
  int guard = 0;
  while (b.undo() && ++guard < 100) {
  }
  if (b.plainText() != "") return false;
  // Redo everything forward again.
  guard = 0;
  while (b.redo() && ++guard < 100) {
  }
  if (b.plainText() != before) return false;
  return true;
}

static bool check_undo_on_empty_returns_false() {
  muse::text::RichBuffer b;
  if (b.undo()) return false;
  if (b.redo()) return false;
  return true;
}

static bool check_redo_cleared_by_new_edit() {
  muse::text::RichBuffer b = makeHello();
  if (!b.undo()) return false;  // back to empty
  b.insert(0, "Hi");
  // ASSUMPTION: a new edit clears the redo stack (standard undo semantics).
  if (b.redo()) return false;
  if (b.plainText() != "Hi") return false;
  return true;
}

static bool check_oversize_payload_and_large_insert_accepted() {
  muse::text::RichBuffer b;
  const std::string big(65536, 'x');
  b.insert(0, big);
  b.setSpan(0, big.size(), muse::text::SpanKind::Color, 0xFFFFFFFFu);
  if (b.plainText() != big) return false;
  return true;
}

// ---- T4: code-editor mode on the same buffer ----

static bool check_expand_tabs_no_tab_returns_length() {
  muse::text::RichBuffer b;
  b.insert(0, "abcd");
  if (muse::text::ExpandTabs(b, 0, 4u) != 4u) return false;
  return true;
}

static bool check_expand_tabs_lands_on_stops() {
  muse::text::RichBuffer b;
  b.insert(0, "a\tb");  // 'a' col 1, tab -> col 4, 'b' col 5
  // ASSUMPTION: ExpandTabs returns the expanded column width in columns.
  if (muse::text::ExpandTabs(b, 0, 4u) != 5u) return false;
  muse::text::RichBuffer c;
  c.insert(0, "\tx");  // tab from col 0 -> col 4, 'x' col 5
  if (muse::text::ExpandTabs(c, 0, 4u) != 5u) return false;
  return true;
}

static bool check_expand_tabs_tabwidth_respected() {
  muse::text::RichBuffer b;
  b.insert(0, "ab\tc");  // cols 2, tab(8) -> 8, 'c' -> 9
  if (muse::text::ExpandTabs(b, 0, 8u) != 9u) return false;
  return true;
}

static bool check_match_bracket_forward_and_back() {
  muse::text::RichBuffer b;
  b.insert(0, "(a[b]c)");
  size_t m = 9999;
  if (!muse::text::MatchBracket(b, 0, &m)) return false;
  if (m != 6u) return false;
  if (!muse::text::MatchBracket(b, 6, &m)) return false;
  if (m != 0u) return false;
  if (!muse::text::MatchBracket(b, 2, &m)) return false;
  if (m != 4u) return false;
  return true;
}

static bool check_match_bracket_non_bracket_false() {
  muse::text::RichBuffer b;
  b.insert(0, "(abc)");
  size_t m = 9999;
  if (muse::text::MatchBracket(b, 1, &m)) return false;
  return true;
}

static bool check_match_bracket_unmatched_false() {
  muse::text::RichBuffer b;
  b.insert(0, "(abc");
  size_t m = 9999;
  if (muse::text::MatchBracket(b, 0, &m)) return false;
  muse::text::RichBuffer e;
  if (muse::text::MatchBracket(e, 0, &m)) return false;
  return true;
}

static bool check_match_bracket_all_pair_kinds() {
  muse::text::RichBuffer b;
  b.insert(0, "()[]{}");
  size_t m = 9999;
  const size_t want[6] = {1, 0, 3, 2, 5, 4};
  for (size_t i = 0; i < 6; ++i) {
    if (!muse::text::MatchBracket(b, i, &m)) return false;
    if (m != want[i]) return false;
  }
  return true;
}

static bool check_line_number_across_multiline() {
  muse::text::RichBuffer b;
  b.insert(0, "aa\nbb\ncc");
  // ASSUMPTION: 0-based line index (first line = 0).
  if (muse::text::LineNumberAt(b, 0) != 0u) return false;
  if (muse::text::LineNumberAt(b, 3) != 1u) return false;
  if (muse::text::LineNumberAt(b, 6) != 2u) return false;
  return true;
}

static bool check_line_number_single_line_zero() {
  muse::text::RichBuffer b = makeHello();
  if (muse::text::LineNumberAt(b, 0) != 0u) return false;
  if (muse::text::LineNumberAt(b, 4) != 0u) return false;
  return true;
}

static bool check_wrap_toggle_does_not_mutate_text() {
  muse::text::RichBuffer b;
  b.insert(0, "aa\n\t(bb)");
  muse::text::CodeOptions wrap{true, 4u, true};
  muse::text::CodeOptions nowrap{false, 4u, true};
  // CodeOptions only parameterizes measurement/wrap computation; the buffer
  // text is untouched however the flags are set. Observable half: text and
  // tab/bracket/line queries are identical (options take no buffer).
  (void)wrap;
  (void)nowrap;
  const std::string before = b.plainText();
  if (muse::text::ExpandTabs(b, 3, wrap.tabWidth) !=
      muse::text::ExpandTabs(b, 3, nowrap.tabWidth))
    return false;
  if (b.plainText() != before) return false;
  if (b.plainText() != "aa\n\t(bb)") return false;
  return true;
}

static bool check_signatures_match_plan() {
  // T3 member-function arity/types via pointer-to-member.
  static_assert(std::is_same_v<decltype(&muse::text::RichBuffer::undo),
                               bool (muse::text::RichBuffer::*)()>);
  static_assert(std::is_same_v<decltype(&muse::text::RichBuffer::redo),
                               bool (muse::text::RichBuffer::*)()>);
  static_assert(std::is_same_v<decltype(&muse::text::RichBuffer::plainText),
                               std::string (muse::text::RichBuffer::*)() const>);
  // T4 free functions.
  static_assert(std::is_invocable_r_v<size_t, decltype(muse::text::ExpandTabs),
                                      const muse::text::RichBuffer&, size_t,
                                      uint32_t>);
  static_assert(std::is_invocable_r_v<bool, decltype(muse::text::MatchBracket),
                                      const muse::text::RichBuffer&, size_t,
                                      size_t*>);
  static_assert(
      std::is_invocable_r_v<uint32_t, decltype(muse::text::LineNumberAt),
                            const muse::text::RichBuffer&, size_t>);
  // T4 options pod shape.
  muse::text::CodeOptions o{true, 4u, true};
  if (!o.softWrap || o.tabWidth != 4u || !o.lineNumbers) return false;
  return true;
}

#define RUN(c)                                        \
  do {                                                \
    if (!c()) {                                       \
      std::printf("FAIL %s\n", #c);                   \
      failures++;                                     \
    }                                                 \
  } while (0)

int main() {
  int failures = 0;
  RUN(check_insert_then_plaintext_roundtrips);
  RUN(check_insert_empty_is_noop);
  RUN(check_erase_removes_range);
  RUN(check_erase_zero_length_is_noop);
  RUN(check_setspan_does_not_change_plaintext);
  RUN(check_all_span_kinds_accepted);
  RUN(check_object_degrades_to_fffc);
  RUN(check_object_mid_text_degrades_inline);
  RUN(check_edit_session_undo_redo_restores_text);
  RUN(check_undo_on_empty_returns_false);
  RUN(check_redo_cleared_by_new_edit);
  RUN(check_oversize_payload_and_large_insert_accepted);
  RUN(check_expand_tabs_no_tab_returns_length);
  RUN(check_expand_tabs_lands_on_stops);
  RUN(check_expand_tabs_tabwidth_respected);
  RUN(check_match_bracket_forward_and_back);
  RUN(check_match_bracket_non_bracket_false);
  RUN(check_match_bracket_unmatched_false);
  RUN(check_match_bracket_all_pair_kinds);
  RUN(check_line_number_across_multiline);
  RUN(check_line_number_single_line_zero);
  RUN(check_wrap_toggle_does_not_mutate_text);
  RUN(check_signatures_match_plan);
  if (failures == 0) std::printf("rich_buffer_test: all passing\n");
  return failures == 0 ? 0 : 1;
}
