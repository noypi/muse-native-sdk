// T4 — Non-ASCII decode: \uXXXX incl. surrogate pairs must reach the
// shaper as correct UTF-8; malformed escapes fail closed, never '?'
// (gallery_present.cpp ParseJsonString, ~line 105).
// FR-TEXT-016 / AC-TEXT-046 (CJK/emoji precondition).
// Hermetic: C++17 + committed runtime headers only; in-memory MUI1 assets,
// no device, no network, no config, no filesystem.
//
// Binds to the plan T4 `Interface:` line (see unit-tests/T4.md).
// ASSUMPTION (surface): the plan's `StatusOr<string>` + `kInvalidEscape`
//   surface is coder-owned; this test pins its observable behavior through
//   the public `ParseGalleryRows` boundary. Per the in-tree fail-closed
//   convention (unparsable value -> row skipped, never a substituted
//   glyph), malformed escapes must drop the row and no emitted label may
//   contain the legacy '?' substitution.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"

namespace {

std::vector<std::uint8_t> MakeAsset(const std::string& json) {
  std::vector<std::uint8_t> out;
  out.reserve(12 + json.size());
  out.push_back('M');
  out.push_back('U');
  out.push_back('I');
  out.push_back('1');
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  std::uint32_t n = static_cast<std::uint32_t>(json.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : json) out.push_back(static_cast<std::uint8_t>(c));
  return out;
}

std::string TextRowJson(const std::string& valueLiteral) {
  return "{\"version\":1,\"roots\":["
         "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
         "\"props\":{},\"slots\":[],\"children\":["
         "{\"type\":\"#text\",\"value\":\"" +
         valueLiteral + "\"}]}]}";
}

std::vector<std::string> LabelsFor(const std::string& valueLiteral,
                                   std::string* error) {
  std::vector<std::uint8_t> asset = MakeAsset(TextRowJson(valueLiteral));
  std::vector<std::string> labels;
  for (const auto& row : runtime::ParseGalleryRows(asset.data(),
                                                   asset.size(), error)) {
    labels.push_back(row.label);
  }
  return labels;
}

bool HasQuestionMark(const std::vector<std::string>& labels) {
  for (const auto& l : labels) {
    if (l.find('?') != std::string::npos) return true;
  }
  return false;
}

// ASCII escapes decode to the mapped character.
static bool check_ascii_escape_decodes() {
  std::string error;
  const auto labels = LabelsFor("\\u0041\\u0042", &error);
  return labels.size() == 1 && labels[0] == "AB";
}

// CJK escapes decode to exact UTF-8 (U+4E2D U+6587).
static bool check_cjk_escapes_decode_to_utf8() {
  std::string error;
  const auto labels = LabelsFor("X\\u4e2d\\u6587", &error);
  if (labels.size() != 1) return false;
  const std::string want("X\xe4\xb8\xad\xe6\x96\x87", 7);
  return labels[0] == want;
}

// RELOCATED-TO-T5: check_cjk_first_label_survives moved to the T5
// emission surface (ASCII-only ROW GATE at gallery_present.cpp:581-585).
// T5's tester pass re-adds it against the emission fix. See unit-tests/T4.md.

// A surrogate pair decodes to one emoji codepoint (U+1F600).
static bool check_emoji_surrogate_pair_decodes() {
  std::string error;
  const auto labels = LabelsFor("X\\uD83D\\uDE00", &error);
  if (labels.size() != 1) return false;
  const std::string want("X\xf0\x9f\x98\x80", 5);
  return labels[0] == want;
}

// Mixed Latin + CJK + emoji round-trips byte-exact in one label.
static bool check_mixed_label_round_trips() {
  std::string error;
  const auto labels = LabelsFor("Hi \\u4e2d \\uD83D\\uDE00!", &error);
  if (labels.size() != 1) return false;
  const std::string want("Hi \xe4\xb8\xad \xf0\x9f\x98\x80!", 12);
  return labels[0] == want;
}

// Lone surrogate fails closed: row dropped, never '?' substituted.
static bool check_lone_surrogate_never_question_mark() {
  std::string error;
  const auto labels = LabelsFor("A\\uD83DB", &error);
  return labels.empty() && !HasQuestionMark(labels);
}

// Truncated escape fails closed: row dropped, never '?'.
static bool check_truncated_escape_fail_closed() {
  std::string error;
  const auto labels = LabelsFor("AB\\u12", &error);
  return labels.empty() && !HasQuestionMark(labels);
}

// Non-hex escape fails closed: row dropped, never '?'.
static bool check_invalid_hex_escape_fail_closed() {
  std::string error;
  const auto labels = LabelsFor("AB\\uXYZZ", &error);
  return labels.empty() && !HasQuestionMark(labels);
}

// Direct UTF-8 CJK bytes (no escape) pass through byte-identical.
static bool check_direct_utf8_cjk_passthrough() {
  std::string error;
  const std::string cjk("X\xe4\xb8\xad", 4);
  const auto labels = LabelsFor(cjk, &error);
  return labels.size() == 1 && labels[0] == cjk;
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);       \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_ascii_escape_decodes);
  RUN_CASE(check_cjk_escapes_decode_to_utf8);
  RUN_CASE(check_emoji_surrogate_pair_decodes);
  RUN_CASE(check_mixed_label_round_trips);
  RUN_CASE(check_lone_surrogate_never_question_mark);
  RUN_CASE(check_truncated_escape_fail_closed);
  RUN_CASE(check_invalid_hex_escape_fail_closed);
  RUN_CASE(check_direct_utf8_cjk_passthrough);
  if (failures > 0) {
    std::printf("gallery_text_decode_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_text_decode_test: all passing\n");
  return 0;
}
