// T11 contract tests — FR-TEXT-013 / AC-TEXT-036 (TC-TEXT-036), test-only.
// No new production API. End-to-end scripted session over the T8/T9/T10
// seams plus a spellcheck-suggestion stub defined HERE (in this test file):
// spellcheck is a stub — session-lifecycle shape only, no
// TextServicesManager on host. Per the plan, the stub returns
// `SuggestionResult{bool accepted; std::string replacement}`; an unknown
// word with no suggestion returns `{false, ""}` and leaves the buffer
// unchanged.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/autofill.h`, `runtime/cursor_rect.h`, and
// `runtime/clipboard.h` do not exist yet, so this file must FAIL TO
// COMPILE. That is the recorded red state.
//
// ASSUMPTION (namespace): T8/T9/T10 names are referenced unqualified
// (global namespace, matching precedent). If the coder nests them, call
// sites reconcile at verify — expectations do not change.
// ASSUMPTION (T8 insert position): inherited from T8 — single-field
// applies on an empty buffer land exactly; sequential multi-field fills
// concatenate in application order.
// ASSUMPTION (T9 geometry/sign): inherited from T9 — origin caret at
// (0,0), `x = offset * char_advance`, `h = line_height`; caret below the
// visible bottom scrolls positive, caret above scrolls negative, visible
// caret scrolls 0.0f.
// ASSUMPTION (T10 paste): inherited from T10 — `paste_rich` inserts at
// `pos`, shifting text right; `degrade_to_plain` returns `plain`.
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/autofill.h"
#include "runtime/clipboard.h"
#include "runtime/cursor_rect.h"
#include "runtime/gap_buffer.h"

// --- spellcheck-suggestion stub (test-local, NOT production) ----------------

struct SuggestionResult {
  bool accepted = false;
  std::string replacement;
};

// Stub dictionary: one known misspelling. Anything else has no suggestion.
static SuggestionResult suggest_stub(const std::string& word) {
  if (word == "teh") {
    return SuggestionResult{true, "the"};
  }
  return SuggestionResult{false, ""};
}

// Applies an accepted suggestion to the buffer range; a declined result
// leaves the buffer unchanged.
static void apply_suggestion_stub(GapBuffer& buf, size_t lo, size_t hi,
                                  const SuggestionResult& r) {
  if (!r.accepted) {
    return;
  }
  buf.erase(lo, hi - lo);
  buf.insert(lo, r.replacement);
}

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

// --- T11 stages (scripted session, maps to AC-TEXT-036) ---------------------

// Stage 1: autofill fills a 3-field form in one tap.
static bool check_stage_autofill_fills_three_field_form() {
  GapBuffer username;
  GapBuffer email;
  GapBuffer phone;
  AutofillField u;
  u.field_id = 1;
  u.value = "bob.test";
  set_autofill_hint(u, AutofillHint::kUsername);
  AutofillField e;
  e.field_id = 2;
  e.value = "bob@example.test";
  set_autofill_hint(e, AutofillHint::kEmail);
  AutofillField p;
  p.field_id = 3;
  p.value = "555-0100";
  set_autofill_hint(p, AutofillHint::kPhone);
  if (!apply_autofill_dataset(username, u)) return false;
  if (!apply_autofill_dataset(email, e)) return false;
  if (!apply_autofill_dataset(phone, p)) return false;
  return username.to_string() == "bob.test" &&
         email.to_string() == "bob@example.test" &&
         phone.to_string() == "555-0100";
}

// Stage 2a: spellcheck stub underlines a misspelling and the accepted
// suggestion is committed.
static bool check_stage_spellcheck_accepts_suggestion() {
  GapBuffer buf;
  buf.insert(0, "teh quick fox");
  SuggestionResult r = suggest_stub("teh");
  if (!r.accepted) return false;
  if (r.replacement != "the") return false;
  apply_suggestion_stub(buf, 0, 3, r);
  return buf.to_string() == "the quick fox";
}

// Stage 2b: unknown word with no suggestion returns {false, ""} and leaves
// the buffer unchanged.
static bool check_stage_spellcheck_unknown_leaves_buffer() {
  GapBuffer buf;
  buf.insert(0, "zxqw word");
  SuggestionResult r = suggest_stub("zxqw");
  if (r.accepted) return false;
  if (!r.replacement.empty()) return false;
  apply_suggestion_stub(buf, 0, 4, r);
  return buf.to_string() == "zxqw word";
}

// Stage 3: copy formatted text + paste preserves styling (plain text
// round-trips; degrade path yields the plain text).
static bool check_stage_clipboard_preserves_paste() {
  GapBuffer src;
  src.insert(0, "styled session text");
  ClipPayload p = copy_rich(src, 0, src.byte_size());
  GapBuffer dst;
  paste_rich(dst, 0, p);
  if (dst.to_string() != "styled session text") return false;
  return degrade_to_plain(p) == "styled session text";
}

// Stage 4: keyboard show/hide keeps the caret in the viewport.
static bool check_stage_keyboard_keeps_caret_visible() {
  CaretRect caret = caret_rect_for_offset(5, 16.0f, 8.0f, 20);
  VisibleViewport vp;
  vp.top = 0.0f;
  vp.height = 400.0f;
  const float heights[] = {0.0f, 120.0f, 250.0f, 120.0f, 0.0f};
  for (float kh : heights) {
    float delta = scroll_to_reveal(caret, vp, kh);
    float visible_bottom = vp.top + vp.height - kh;
    float caret_bottom = caret.y + caret.h - delta;
    float caret_top = caret.y - delta;
    if (caret_top < vp.top) return false;
    if (caret_bottom > visible_bottom) return false;
  }
  return true;
}

// Full session: all four stages pass in one run over shared seams.
static bool check_full_depth_session_all_stages() {
  if (!check_stage_autofill_fills_three_field_form()) return false;
  if (!check_stage_spellcheck_accepts_suggestion()) return false;
  if (!check_stage_spellcheck_unknown_leaves_buffer()) return false;
  if (!check_stage_clipboard_preserves_paste()) return false;
  if (!check_stage_keyboard_keeps_caret_visible()) return false;
  return true;
}

// Error shape: stub contract — accepted carries a replacement, declined is
// exactly {false, ""}.
static bool check_suggestion_error_shape() {
  SuggestionResult ok = suggest_stub("teh");
  if (!ok.accepted || ok.replacement.empty()) return false;
  SuggestionResult no = suggest_stub("definitely-not-a-word-xyz");
  return !no.accepted && no.replacement.empty();
}

// Error path: extreme inputs across the seams never throw.
static bool check_extreme_inputs_never_throw() {
  try {
    GapBuffer buf;
    AutofillField f;
    f.field_id = 1;
    f.value = std::string(4096, 'q');
    set_autofill_hint(f, AutofillHint::kUsername);
    (void)apply_autofill_dataset(buf, f);
    (void)suggest_stub(std::string(4096, 'z'));
    ClipPayload p = copy_rich(buf, 0, 18446744073709551615ULL);
    paste_rich(buf, 18446744073709551615ULL, p);
    CaretRect caret = caret_rect_for_offset(18446744073709551615ULL, 16.0f,
                                            8.0f, 20);
    VisibleViewport vp;
    vp.top = 0.0f;
    vp.height = 400.0f;
    (void)scroll_to_reveal(caret, vp, 250.0f);
    VisibleViewport zero;
    zero.top = 0.0f;
    zero.height = 0.0f;
    if (scroll_to_reveal(caret, zero, 250.0f) != 0.0f) return false;
  } catch (...) {
    return false;
  }
  return true;
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
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_stage_autofill_fills_three_field_form);
  RUN_CASE(check_stage_spellcheck_accepts_suggestion);
  RUN_CASE(check_stage_spellcheck_unknown_leaves_buffer);
  RUN_CASE(check_stage_clipboard_preserves_paste);
  RUN_CASE(check_stage_keyboard_keeps_caret_visible);
  RUN_CASE(check_full_depth_session_all_stages);
  RUN_CASE(check_suggestion_error_shape);
  RUN_CASE(check_extreme_inputs_never_throw);
  if (failures > 0) {
    std::printf("ime_depth_test: %d failing\n", failures);
    return 1;
  }
  std::printf("ime_depth_test: all passing\n");
  return 0;
}
