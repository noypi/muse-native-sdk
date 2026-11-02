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
#include "muse/text/font_discovery.h"

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

// --- Batch 1 (BUG-TEXT-008) T1: host hermetic fail-path pin.
// shapeRunWithFallback rejects malformed UTF-8 on every lane (never shapes
// attacker bytes). Holds GREEN on host; guards the fail-closed contract. ---

static bool check_fallback_malformed_input_fails() {
  const uint8_t kBad[] = {0xE4, 0xB8};
  try {
    muse::text::TextStyle style{};
    muse::text::FontRequest req{};
    auto result = muse::text::shapeRunWithFallback(kBad, sizeof(kBad), style,
                                                   req);
    return !result.ok();
  } catch (...) {
    return false;
  }
}

// --- Batch 1 (BUG-TEXT-008) T1: device-leg failing pin (DEVICE_GATED).
// TRUE leg runs on the x86_64 emulator (127.0.0.1:6555) at verify time.
// Host SKIP-passes so the hermetic suite stays green; never faked.
// Pre-fix on device this FAILS (ResolveRealFont false -> kMissingGlyph);
// post-fix it PASSES (Roboto labels shape, drawn>0).
// NOTE: ResolveRealFont is TU-local to shaper.cpp (no header decl), so the
// device leg pins it behaviorally through the public shapeRunWithFallback
// boundary, per the plan Interface line. ---

#if defined(__ANDROID__)
static bool check_device_shape_run_with_fallback_latin_label_ok() {
  const char kLabel[] = "m3-gallery";
  try {
    muse::text::TextStyle style{};
    muse::text::FontRequest req{};
    auto result = muse::text::shapeRunWithFallback(
        reinterpret_cast<const uint8_t*>(kLabel), sizeof(kLabel) - 1, style,
        req);
    if (!result.ok()) return false;
    return !(*result).glyph_ids.empty();
  } catch (...) {
    return false;
  }
}
#else
static bool check_device_shape_run_with_fallback_latin_label_ok() {
  std::printf("SKIP device-gated (needs __ANDROID__ 6555 leg)\n");
  return true;
}
#endif

// --- Batch 1 (BUG-TEXT-008) T2: reason-coded fail-closed contract,
// shaper side. Closest observable host behavior (TC-TEXT-047 grep
// precedent): shaper.cpp must log FT_Init_FreeType/FT_New_Face return codes
// and name the hb/missing-glyph reasons from the plan vocabulary
// (`[reason=path|ft-open|hb|missing-glyph]`). RED now, GREEN after the coder
// instruments. Same ASSUMPTION as system_fonts_test.cpp on exact spelling. ---

static bool source_file_contains_shaper(const std::string& rel,
                                        const char* token) {
  const std::string path = source_dir() + "/" + rel;
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in.good()) return false;
  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  return content.find(token) != std::string::npos;
}

static bool check_shaper_logs_ft_init_and_new_face_return_codes() {
  const char* kFile = "../src/shaper.cpp";
  return source_file_contains_shaper(kFile, "__android_log_print") &&
         (source_file_contains_shaper(kFile, "FT_Error") ||
          source_file_contains_shaper(kFile, "ft_err"));
}

static bool check_shaper_names_hb_and_missing_glyph_reasons() {
  const char* kFile = "../src/shaper.cpp";
  return source_file_contains_shaper(kFile, "reason=hb") &&
         source_file_contains_shaper(kFile, "reason=missing-glyph");
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

// --- Follow-up batch (BUG-TEXT-008 T5-FAIL, 2026-11-03). Device proof on
// 6555 (workflow.local/runs/bug-text-008/device-proof/logcat-t5.txt +
// test-report.md T5 section): the script-agnostic gallery request arrives
// with script_tag=0; only the last-resort face (id 16777215, file-less) is
// ever probed ([reason=path], no file candidates); device Roboto files
// exist but no Roboto face enters the chain; 5/5 frames drawn=0
// [reason=missing-glyph]. These hermetic pins lock the fixed behavior on
// host through the public font_discovery boundary (Real-only: no Fake, the
// stub-backed enumerator IS the ordering-logic Real on host; the NDK
// enumerator plugs the same seam on device). All three are RED pre-fix by
// construction (SupportsScript(0)==false today) and GREEN post-fix.
// ASSUMPTION: unset script (0) must default to Latn routing (or script
// detection must route Latin text to Latn) — the gallery never sends a tag.
// ASSUMPTION: 'Latn' tag literal 0x4C61746E and LastResort id 0x00FFFFFF
// mirror font_discovery.cpp; if the coder renames the constants (not the
// behavior), update the literals, not the expectations (GR-08 T1.9). ---

namespace {
constexpr std::uint32_t kPinLatnTag = 0x4C61746Eu;  // 'Latn'
constexpr std::uint64_t kPinLastResortId = 0x00FFFFFFu;  // id 16777215
}  // namespace

// T5a: script-0 defaults to Latn — resolveFallbackChain(0) must resolve a
// real (non-last-resort) face, never terminate at the file-less LastResort.
static bool check_script_zero_defaults_to_latn_face() {
  try {
    // Positive control: explicit Latn already resolves a real face, so a
    // T5a failure is specific to the unset-script default, not the chain.
    auto latn = muse::text::resolveFallbackChain(kPinLatnTag, nullptr,
                                                 nullptr);
    if (!latn.ok() || (*latn).id == kPinLastResortId) return false;
    auto result = muse::text::resolveFallbackChain(0, nullptr, nullptr);
    if (!result.ok()) return false;
    return (*result).id != kPinLastResortId;
  } catch (...) {
    return false;
  }
}

// T5b: the file-less last-resort face never terminates a resolvable chain
// alone — discoverFonts for the unset script must succeed with at least one
// non-last-resort face ahead of the LastResort tail.
static bool check_unset_script_chain_has_resolvable_face_before_lastresort() {
  try {
    muse::text::FontQuery q;
    q.scriptTag = 0;
    auto result = muse::text::discoverFonts(q);
    if (!result.ok()) return false;
    const std::vector<muse::text::FontHandle>& chain = *result;
    if (chain.size() < 2) return false;
    if (chain.back().id != kPinLastResortId) return false;
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
      if (chain[i].id != kPinLastResortId) return true;
    }
    return false;
  } catch (...) {
    return false;
  }
}

// T5c: Roboto candidate faces enter the chain for Latin text arriving with
// an unset script — FacesForFamily("Roboto", 0) must be non-empty and carry
// no last-resort face.
static bool check_roboto_covers_unset_script_latin() {
  try {
    const muse::text::SystemFontEnumerator enumerator;
    const std::vector<muse::text::FontHandle> faces =
        enumerator.FacesForFamily("Roboto", 0);
    if (faces.empty()) return false;
    for (std::size_t i = 0; i < faces.size(); ++i) {
      if (faces[i].id == kPinLastResortId) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

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
  RUN_CASE(check_fallback_malformed_input_fails);
  RUN_CASE(check_device_shape_run_with_fallback_latin_label_ok);
  RUN_CASE(check_shaper_logs_ft_init_and_new_face_return_codes);
  RUN_CASE(check_shaper_names_hb_and_missing_glyph_reasons);
  RUN_CASE(check_script_zero_defaults_to_latn_face);
  RUN_CASE(check_unset_script_chain_has_resolvable_face_before_lastresort);
  RUN_CASE(check_roboto_covers_unset_script_latin);
  if (failures > 0) {
    std::printf("script_shaping_test: %d failing\n", failures);
    return 1;
  }
  std::printf("script_shaping_test: all passing\n");
  return 0;
}
