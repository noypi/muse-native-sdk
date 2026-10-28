// T5 — AC-TEXT-022 / FR-TEXT-010: per-script shaping validated against
// checked-in HarfBuzz reference dumps (tests/data/harfbuzz_ref.tsv).
// The plan Interface line reuses the COMMITTED shaper with an unchanged
// signature:
//   muse::text::StatusOr<muse::text::ShapedRun>
//   muse::text::shapeRun(const uint8_t* bytes, size_t len,
//                        muse::text::FontHandle font,
//                        const muse::text::TextStyle& style)
// so this file COMPILES today; the expected RED is behavioral (the
// committed stub backend returns codepoint-as-id / 9.0 / 11.0 advances,
// never the TSV reference ids/fractional advances), not a malformed test.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no live HarfBuzz, no downloads. The TSV is read from the source
// tree relative to this file (__FILE__), so the test is independent of the
// runner's working directory. Mismatch on any row fails the case.
// Shape mirrors engine/text/tests/font_fallback_test.cpp (Batch 1 T1):
// static bool check_*() cases + main, return 0 all-green / non-zero +
// failing name.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "muse/text/shaper.h"

// ASSUMPTION (structural): shapeRun keeps its committed signature; the T5
// coder swaps the stub backend for reference-matching shaping WITHOUT
// changing the signature (plan: "signature unchanged") and WITHOUT linking
// live HarfBuzz into the test. A coder that changes the signature instead
// is an interface dispute (GR-08 T1.10): stop, do not adapt the test.
// ASSUMPTION: advances are compared with 1e-3 px tolerance to survive the
// decimal text round-trip in the TSV; glyph_ids are compared exactly.
// ASSUMPTION: reference ids/advances in harfbuzz_ref.tsv are the
// authoritative dumps (plan assumption A3); the test never derives
// expectations from the implementation.
// ASSUMPTION: zero-JNI holds structurally (shaper.h pulls no JNI/NDK
// types); verified by compilation, not by a runtime case.

namespace {

struct RefRow {
  std::string label;
  std::vector<uint8_t> text;
  std::vector<uint32_t> glyph_ids;
  std::vector<float> advances;
};

std::string source_dir() {
  std::string file = __FILE__;
  std::string::size_type slash = file.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return file.substr(0, slash);
}

bool parse_hex_bytes(const std::string& hex, std::vector<uint8_t>* out) {
  if (hex.size() % 2 != 0) return false;
  out->clear();
  for (std::size_t i = 0; i < hex.size(); i += 2) {
    unsigned int byte = 0;
    if (std::sscanf(hex.c_str() + i, "%2x", &byte) != 1) return false;
    out->push_back(static_cast<uint8_t>(byte));
  }
  return true;
}

bool parse_u32_list(const std::string& csv, std::vector<uint32_t>* out) {
  out->clear();
  std::string cur;
  for (std::size_t i = 0; i <= csv.size(); ++i) {
    char c = i < csv.size() ? csv[i] : ',';
    if (c == ',') {
      if (cur.empty()) return false;
      unsigned long v = 0;
      if (std::sscanf(cur.c_str(), "%lu", &v) != 1) return false;
      out->push_back(static_cast<uint32_t>(v));
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  return !out->empty();
}

bool parse_float_list(const std::string& csv, std::vector<float>* out) {
  out->clear();
  std::string cur;
  for (std::size_t i = 0; i <= csv.size(); ++i) {
    char c = i < csv.size() ? csv[i] : ',';
    if (c == ',') {
      if (cur.empty()) return false;
      float v = 0.0f;
      if (std::sscanf(cur.c_str(), "%f", &v) != 1) return false;
      out->push_back(v);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  return !out->empty();
}

bool load_rows(std::vector<RefRow>* rows) {
  rows->clear();
  const std::string candidates[] = {
      source_dir() + "/data/harfbuzz_ref.tsv",
      "engine/text/tests/data/harfbuzz_ref.tsv",
      "tests/data/harfbuzz_ref.tsv",
      "./harfbuzz_ref.tsv",
  };
  std::string used;
  std::ifstream in;
  for (std::size_t i = 0; i < 4; ++i) {
    in.open(candidates[i]);
    if (in.is_open()) {
      used = candidates[i];
      break;
    }
    in.clear();
  }
  if (!in.is_open()) {
    std::printf("harfbuzz_ref.tsv: not found (tried %s)\n",
                candidates[0].c_str());
    return false;
  }
  (void)used;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
    std::vector<std::string> cols;
    std::string cur;
    for (char c : line) {
      if (c == '\t') {
        cols.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    cols.push_back(cur);
    if (cols.size() != 5) return false;
    if (cols[0] == "label") continue;  // skip TSV header row (not a RefRow)
    RefRow row;
    row.label = cols[0];
    if (!parse_hex_bytes(cols[2], &row.text)) return false;
    if (!parse_u32_list(cols[3], &row.glyph_ids)) return false;
    if (!parse_float_list(cols[4], &row.advances)) return false;
    rows->push_back(row);
  }
  return !rows->empty();
}

const RefRow* find_row(const std::vector<RefRow>& rows, const char* label) {
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (rows[i].label == label) return &rows[i];
  }
  return nullptr;
}

bool row_matches(const char* label) {
  try {
    std::vector<RefRow> rows;
    if (!load_rows(&rows)) return false;
    const RefRow* row = find_row(rows, label);
    if (row == nullptr) {
      std::printf("row %s: missing from tsv\n", label);
      return false;
    }
    muse::text::FontHandle font{7};
    muse::text::TextStyle style{};
    muse::text::StatusOr<muse::text::ShapedRun> result =
        muse::text::shapeRun(row->text.data(), row->text.size(), font, style);
    if (!result.ok()) return false;
    const muse::text::ShapedRun& run = *result;
    if (run.glyph_ids.size() != row->glyph_ids.size()) return false;
    if (run.advances.size() != row->advances.size()) return false;
    for (std::size_t i = 0; i < row->glyph_ids.size(); ++i) {
      if (run.glyph_ids[i] != row->glyph_ids[i]) return false;
    }
    for (std::size_t i = 0; i < row->advances.size(); ++i) {
      if (std::fabs(run.advances[i] - row->advances[i]) > 0.001f) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

}  // namespace

static bool check_latin_basic_row() { return row_matches("latin_basic"); }

static bool check_latin_control_row() { return row_matches("latin_control"); }

static bool check_arabic_joining_row() { return row_matches("arabic_joining"); }

static bool check_hebrew_row() { return row_matches("hebrew"); }

static bool check_devanagari_row() { return row_matches("devanagari"); }

static bool check_deva_matra_reorder_row() { return row_matches("deva_matra"); }

static bool check_thai_row() { return row_matches("thai"); }

static bool check_khmer_row() { return row_matches("khmer"); }

static bool check_myanmar_row() { return row_matches("myanmar"); }

static bool check_hani_row() { return row_matches("hani"); }

static bool check_malformed_input_fails() {
  // Truncated 3-byte lead: shapeRun must reject, never match a row.
  const uint8_t kBad[] = {0xE4, 0xB8};
  try {
    muse::text::FontHandle font{7};
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRun(kBad, sizeof(kBad), font, style);
    return !result.ok();
  } catch (...) {
    return false;
  }
}

static bool check_empty_input_ok_empty() {
  const uint8_t kEmpty[] = {0x00};
  try {
    muse::text::FontHandle font{7};
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRun(kEmpty, 0, font, style);
    if (!result.ok()) return false;
    return (*result).glyph_ids.empty() && (*result).advances.empty();
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                 \
  do {                                 \
    if (!(name)()) {                   \
      std::printf("FAIL %s\n", #name); \
      ++failures;                      \
    } else {                           \
      std::printf("PASS %s\n", #name); \
    }                                  \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_latin_basic_row);
  RUN_CASE(check_latin_control_row);
  RUN_CASE(check_arabic_joining_row);
  RUN_CASE(check_hebrew_row);
  RUN_CASE(check_devanagari_row);
  RUN_CASE(check_deva_matra_reorder_row);
  RUN_CASE(check_thai_row);
  RUN_CASE(check_khmer_row);
  RUN_CASE(check_myanmar_row);
  RUN_CASE(check_hani_row);
  RUN_CASE(check_malformed_input_fails);
  RUN_CASE(check_empty_input_ok_empty);
  if (failures > 0) {
    std::printf("script_shaping_test: %d failing\n", failures);
    return 1;
  }
  std::printf("script_shaping_test: all passing\n");
  return 0;
}
