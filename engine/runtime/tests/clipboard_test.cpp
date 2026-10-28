// T10 contract tests — FR-TEXT-013 / AC-TEXT-035 (TC-TEXT-035).
// Binds ONLY to the T10 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
// Expected red state: `runtime/clipboard.h` does not exist yet, so this
// file must FAIL TO COMPILE. That is the recorded red state.
//
// ASSUMPTION (namespace): `ClipPayload`/`copy_rich`/`paste_rich`/
// `degrade_to_plain` are referenced unqualified (global namespace, matching
// the GapBuffer/ALooperBridge precedent). If the coder nests them (e.g.
// `runtime::`), call sites reconcile at verify — expectations do not
// change.
// ASSUMPTION (payload shape): the Interface line does not pin the html
// serialization. The tests assert only that `plain` carries the exact
// buffer text, that `degrade_to_plain` returns `plain`, and that pasting
// reproduces the plain text in the buffer — never an exact html string.
// ASSUMPTION (paste position): `paste_rich(buf, pos, p)` inserts the
// payload text at `pos` (clamped to buffer bounds), shifting existing text
// right. The Interface line names `pos` but not the semantics; insert (not
// overwrite) is the most defensible reading for a paste operation.
#include <cstdio>
#include <cstring>
#include <string>

#include "runtime/clipboard.h"
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

// --- T10 cases (each maps to the Interface: line + AC-TEXT-035) -------------

// Happy path: copying a range captures its exact plain text.
static bool check_copy_captures_plain_text() {
  GapBuffer buf;
  buf.insert(0, "hello world");
  ClipPayload p = copy_rich(buf, 0, 5);
  return p.plain == "hello";
}

// Happy path: copying the whole buffer round-trips through paste.
static bool check_copy_paste_round_trip() {
  GapBuffer src;
  src.insert(0, "styled text");
  ClipPayload p = copy_rich(src, 0, src.byte_size());
  GapBuffer dst;
  paste_rich(dst, 0, p);
  return dst.to_string() == "styled text";
}

// Happy path: paste inserts at the position, shifting text right.
static bool check_paste_inserts_at_position() {
  GapBuffer buf;
  buf.insert(0, "ac");
  ClipPayload p;
  p.plain = "b";
  paste_rich(buf, 1, p);
  return buf.to_string() == "abc";
}

// Happy path: degrade path yields the plain text for plain-text targets.
static bool check_degrade_yields_plain_text() {
  GapBuffer buf;
  buf.insert(0, "plain target");
  ClipPayload p = copy_rich(buf, 0, buf.byte_size());
  return degrade_to_plain(p) == "plain target";
}

// Happy path: degrade of a hand-built payload returns its plain field.
static bool check_degrade_returns_plain_field() {
  ClipPayload p;
  p.plain = "hello";
  p.html = "<b>hello</b>";
  p.has_image = false;
  return degrade_to_plain(p) == "hello";
}

// Boundary: out-of-range [lo, hi) clamps to buffer bounds.
static bool check_copy_out_of_range_clamps() {
  GapBuffer buf;
  buf.insert(0, "abc");
  ClipPayload p = copy_rich(buf, 0, 1000000);
  if (p.plain != "abc") return false;
  ClipPayload q = copy_rich(buf, 1000000, 2000000);
  return q.plain.empty();
}

// Boundary: inverted range clamps to empty.
static bool check_copy_inverted_range_is_empty() {
  GapBuffer buf;
  buf.insert(0, "abc");
  ClipPayload p = copy_rich(buf, 2, 1);
  return p.plain.empty();
}

// Boundary: empty buffer copies to an empty payload.
static bool check_copy_empty_buffer_is_empty() {
  GapBuffer buf;
  ClipPayload p = copy_rich(buf, 0, 0);
  GapBuffer dst;
  dst.insert(0, "keep");
  paste_rich(dst, 0, p);
  return p.plain.empty() && dst.to_string() == "keep";
}

// Boundary: paste with an empty payload is a no-op.
static bool check_empty_payload_paste_is_noop() {
  GapBuffer buf;
  buf.insert(0, "keep");
  ClipPayload p;
  p.plain = "";
  p.html = "";
  p.has_image = false;
  paste_rich(buf, 1, p);
  return buf.to_string() == "keep";
}

// Boundary: paste position past the end clamps to the end.
static bool check_paste_past_end_clamps() {
  GapBuffer buf;
  buf.insert(0, "ab");
  ClipPayload p;
  p.plain = "c";
  paste_rich(buf, 1000000, p);
  return buf.to_string() == "abc";
}

// Error path: extreme inputs never throw and keep buffers usable.
static bool check_extreme_inputs_never_throw() {
  GapBuffer buf;
  try {
    buf.insert(0, "abc");
    ClipPayload p = copy_rich(buf, 18446744073709551615ULL,
                               18446744073709551615ULL);
    (void)p.plain;
    ClipPayload big;
    big.plain = std::string(65536, 'y');
    paste_rich(buf, 18446744073709551615ULL, big);
    ClipPayload empty;
    (void)degrade_to_plain(empty);
    (void)buf.to_string();
  } catch (...) {
    return false;
  }
  return true;
}

// F1 (review MAJOR — clipboard styling): html carries span markup when
// spans overlap the copied range. Pins only: html differs from plain and
// still contains the plain text. Does NOT pin exact tag names.
static bool check_copy_html_carries_span_markup() {
  GapBuffer buf;
  buf.insert(0, "hello world");
  buf.tag_span(0, 5, 1);
  ClipPayload p = copy_rich(buf, 0, 5);
  if (p.plain != "hello") return false;
  if (p.html.empty()) return false;
  if (p.html == p.plain) return false;
  return p.html.find("hello") != std::string::npos;
}

// F1 (review MAJOR — clipboard styling): paste into a second buffer
// restores spans of the same kinds (not just text). Span kinds are opaque
// uint32 — pinned only via kinds stored through tag_span (1, 2).
static bool check_paste_restores_spans() {
  GapBuffer src;
  src.insert(0, "hello world");
  src.tag_span(0, 5, 1);
  src.tag_span(6, 11, 2);
  ClipPayload p = copy_rich(src, 0, src.byte_size());
  GapBuffer dst;
  paste_rich(dst, 0, p);
  if (dst.to_string() != "hello world") return false;
  if (dst.spans_of_kind(1).empty()) return false;
  if (dst.spans_of_kind(2).empty()) return false;
  return true;
}

// F1: degrade_to_plain is unchanged — still returns plain even with spans.
static bool check_degrade_unchanged_with_spans() {
  GapBuffer buf;
  buf.insert(0, "hello world");
  buf.tag_span(0, 5, 1);
  ClipPayload p = copy_rich(buf, 0, 5);
  return degrade_to_plain(p) == "hello";
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
  RUN_CASE(check_copy_captures_plain_text);
  RUN_CASE(check_copy_paste_round_trip);
  RUN_CASE(check_paste_inserts_at_position);
  RUN_CASE(check_degrade_yields_plain_text);
  RUN_CASE(check_degrade_returns_plain_field);
  RUN_CASE(check_copy_out_of_range_clamps);
  RUN_CASE(check_copy_inverted_range_is_empty);
  RUN_CASE(check_copy_empty_buffer_is_empty);
  RUN_CASE(check_empty_payload_paste_is_noop);
  RUN_CASE(check_paste_past_end_clamps);
  RUN_CASE(check_extreme_inputs_never_throw);
  RUN_CASE(check_copy_html_carries_span_markup);
  RUN_CASE(check_paste_restores_spans);
  RUN_CASE(check_degrade_unchanged_with_spans);
  if (failures > 0) {
    std::printf("clipboard_test: %d failing\n", failures);
    return 1;
  }
  std::printf("clipboard_test: all passing\n");
  return 0;
}
