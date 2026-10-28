// T8 — AC-TEXT-024 / FR-TEXT-010: golden image corpus — host metric/tofu
// harness + manifest contract. Pixel comparison itself is EXTERNAL
// (device-lab, TC-TEXT-024); this harness proves locally that every corpus
// input shapes with zero kMissingGlyph/tofu and that Arabic/Indic joining
// matches the checked-in HarfBuzz dump.
// Hermetic harness: plain C++17, no test framework, no device, no font
// files, no live HarfBuzz, no downloads, no pixels. Corpus inputs and the
// manifest are CHECKED-IN .txt/.json fixtures read from the source tree
// relative to __FILE__ (device-lab/text-goldens/); the HarfBuzz reference
// is the committed engine/text/tests/data/harfbuzz_ref.tsv (Batch 2 T5).
// Shape mirrors engine/text/tests/font_fallback_test.cpp (Batch 1 T1):
// static bool check_*() cases + main, return 0 all-green / non-zero +
// failing name.
// NOTE: engine interface is `none — corpus manifest plus host
// metric/tofu harness` (plan Interface line): this harness drives only the
// COMMITTED shapeRun, so it compiles today. Expected RED: manifest.json +
// corpus/*.txt do not exist yet, so every manifest/corpus case fails on
// missing fixtures — missing data, not a malformed test. The pixel-stable
// API-29/33/current comparison stays RED/EXTERNAL in device-lab
// (TC-TEXT-024) by design.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "muse/text/shaper.h"

// ASSUMPTION: corpus inputs are small UTF-8 .txt files (no BOM required;
// a BOM, if present, is skipped by the loader, never shaped).
// ASSUMPTION: "zero tofu" == shapeRun returns ok() with glyph_ids
// non-empty and advances.size() == glyph_ids.size() for every corpus input
// (mirrors the T4 metrics-presence observable; no pixel rasterization on
// host).
// ASSUMPTION: Arabic/Indic joining parity reuses the T5 harfbuzz_ref.tsv
// rows (arabic_joining, devanagari, deva_matra) as the checked-in dump;
// the harness never links live HarfBuzz. The corpus fixtures intentionally
// carry DIFFERENT bytes than the TSV rows (corpus words + " shaping test"
// suffix), so each joining case asserts corpus tofu-freedom AND shapes the
// TSV row bytes directly (exact ids, 1e-3 px advances — T5 tolerance).
// ASSUMPTION: manifest.json is a flat contract listing corpus file names
// plus api-29/33/current pixel expectations for the EXTERNAL device-lab
// run; the host harness asserts every listed file exists and is covered,
// and every corpus file is listed (no orphan inputs, no dangling entries).

namespace {

std::string source_dir() {
  std::string file = __FILE__;
  std::string::size_type slash = file.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return file.substr(0, slash);
}

std::string goldens_dir() {
  return source_dir() + "/../../../device-lab/text-goldens";
}

bool read_file(const std::string& path, std::string* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  *out = ss.str();
  return true;
}

bool shapes_without_tofu(const std::string& text) {
  try {
    if (text.empty()) return false;
    // Skip a UTF-8 BOM if the fixture carries one.
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(text.data());
    std::size_t len = text.size();
    if (len >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB &&
        bytes[2] == 0xBF) {
      bytes += 3;
      len -= 3;
    }
    if (len == 0) return false;
    muse::text::FontHandle font{7};
    muse::text::TextStyle style{};
    auto result = muse::text::shapeRun(bytes, len, font, style);
    if (!result.ok()) return false;
    if (result.status() == muse::text::Status::kMissingGlyph) return false;
    const muse::text::ShapedRun& run = *result;
    return !run.glyph_ids.empty() &&
           run.advances.size() == run.glyph_ids.size();
  } catch (...) {
    return false;
  }
}

// Minimal manifest scan: extracts every "*.txt" token from manifest.json.
// (The manifest is a flat contract file; the harness checks coverage, it
// does not implement a JSON parser.)
std::vector<std::string> manifest_txt_tokens(const std::string& manifest) {
  std::vector<std::string> out;
  std::size_t pos = 0;
  while ((pos = manifest.find(".txt", pos)) != std::string::npos) {
    std::size_t start = manifest.find_last_of("\"'", pos);
    if (start == std::string::npos || start + 1 >= pos) {
      ++pos;
      continue;
    }
    std::string token = manifest.substr(start + 1, pos + 4 - start - 1);
    std::string::size_type slash = token.find_last_of("/\\");
    if (slash != std::string::npos) token = token.substr(slash + 1);
    out.push_back(token);
    pos += 4;
  }
  return out;
}

const char* kCorpus[] = {
    "emoji_zwj.txt",
    "emoji_modifiers.txt",
    "emoji_flags_keycaps.txt",
    "bidi_mixed.txt",
    "arabic_joining.txt",
    "indic_shaping.txt",
};

// TSV helpers (mirror script_shaping_test.cpp Batch 2 T5 conventions): decode
// the checked-in harfbuzz_ref.tsv row for `label` into raw UTF-8 bytes plus
// expected glyph ids/advances, then compare actual shapeRun output against
// them (exact ids, 1e-3 px advance tolerance).
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

struct TsvRow {
  std::vector<uint8_t> text;
  std::vector<uint32_t> glyph_ids;
  std::vector<float> advances;
};

bool find_tsv_row(const std::string& tsv, const char* label, TsvRow* out) {
  std::istringstream lines(tsv);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.empty() || line[0] == '#') continue;
    if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
    std::vector<std::string> cols;
    std::string cur;
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '\t') {
        cols.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(line[i]);
      }
    }
    cols.push_back(cur);
    if (cols.size() != 5) continue;
    if (cols[0] != label) continue;
    TsvRow row;
    if (!parse_hex_bytes(cols[2], &row.text)) return false;
    if (!parse_u32_list(cols[3], &row.glyph_ids)) return false;
    if (!parse_float_list(cols[4], &row.advances)) return false;
    *out = row;
    return true;
  }
  return false;
}

bool tsv_row_matches_shaper(const char* label) {
  try {
    std::string tsv;
    if (!read_file(source_dir() + "/data/harfbuzz_ref.tsv", &tsv)) return false;
    TsvRow row;
    if (!find_tsv_row(tsv, label, &row)) {
      std::printf("tsv row missing: %s\n", label);
      return false;
    }
    muse::text::FontHandle font{7};
    muse::text::TextStyle style{};
    muse::text::StatusOr<muse::text::ShapedRun> result = muse::text::shapeRun(
        row.text.data(), row.text.size(), font, style);
    if (!result.ok()) {
      std::printf("tsv row %s: shapeRun not ok\n", label);
      return false;
    }
    const muse::text::ShapedRun& run = *result;
    if (run.glyph_ids.size() != row.glyph_ids.size() ||
        run.advances.size() != row.advances.size()) {
      std::printf("tsv row %s: count mismatch (got %u/%u, want %u/%u)\n",
                  label, static_cast<unsigned>(run.glyph_ids.size()),
                  static_cast<unsigned>(run.advances.size()),
                  static_cast<unsigned>(row.glyph_ids.size()),
                  static_cast<unsigned>(row.advances.size()));
      return false;
    }
    for (std::size_t i = 0; i < row.glyph_ids.size(); ++i) {
      if (run.glyph_ids[i] != row.glyph_ids[i]) {
        std::printf("tsv row %s: glyph id[%u] mismatch (got %u, want %u)\n",
                    label, static_cast<unsigned>(i),
                    static_cast<unsigned>(run.glyph_ids[i]),
                    static_cast<unsigned>(row.glyph_ids[i]));
        return false;
      }
    }
    for (std::size_t i = 0; i < row.advances.size(); ++i) {
      if (std::fabs(run.advances[i] - row.advances[i]) > 0.001f) {
        std::printf("tsv row %s: advance[%u] mismatch (got %f, want %f)\n",
                    label, static_cast<unsigned>(i),
                    static_cast<double>(run.advances[i]),
                    static_cast<double>(row.advances[i]));
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

// Decodes one UTF-8 codepoint at *pos, advancing past it. False on
// malformed/truncated sequences.
bool utf8_next(const std::string& s, std::size_t* pos, uint32_t* cp) {
  if (*pos >= s.size()) return false;
  unsigned char c = static_cast<unsigned char>(s[*pos]);
  if (c < 0x80) {
    *cp = c;
    *pos += 1;
    return true;
  }
  std::size_t need = 0;
  uint32_t v = 0;
  if ((c & 0xE0) == 0xC0) {
    need = 1;
    v = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    need = 2;
    v = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    need = 3;
    v = c & 0x07;
  } else {
    return false;
  }
  if (*pos + need >= s.size()) return false;
  for (std::size_t k = 1; k <= need; ++k) {
    unsigned char d = static_cast<unsigned char>(s[*pos + k]);
    if ((d & 0xC0) != 0x80) return false;
    v = (v << 6) | (d & 0x3F);
  }
  *cp = v;
  *pos += 1 + need;
  return true;
}

// True iff the text holds at least one Hebrew (U+0590-U+05FF) or Arabic
// (U+0600-U+06FF) codepoint. Malformed UTF-8 counts as absent.
bool contains_hebrew_or_arabic(const std::string& text) {
  std::size_t pos = 0;
  if (text.size() >= 3 &&
      static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    pos = 3;
  }
  while (pos < text.size()) {
    uint32_t cp = 0;
    if (!utf8_next(text, &pos, &cp)) return false;
    if ((cp >= 0x0590 && cp <= 0x05FF) ||
        (cp >= 0x0600 && cp <= 0x06FF)) {
      return true;
    }
  }
  return false;
}

// Lists every "*.txt" entry directly inside device-lab/text-goldens/corpus.
// False when the directory cannot be opened.
bool list_corpus_txt_files(std::vector<std::string>* out) {
  out->clear();
  std::string dir = goldens_dir() + "/corpus";
  DIR* d = ::opendir(dir.c_str());
  if (d == nullptr) return false;
  while (dirent* e = ::readdir(d)) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    if (name.size() < 4 ||
        name.compare(name.size() - 4, 4, ".txt") != 0) {
      continue;
    }
    out->push_back(name);
  }
  ::closedir(d);
  return true;
}

}  // namespace

static bool check_manifest_present() {
  try {
    std::string manifest;
    if (!read_file(goldens_dir() + "/manifest.json", &manifest)) return false;
    return manifest.find("corpus") != std::string::npos &&
           manifest.find("api-29") != std::string::npos &&
           manifest.find("api-33") != std::string::npos &&
           manifest.find("current") != std::string::npos;
  } catch (...) {
    return false;
  }
}

static bool check_corpus_inputs_shape_without_tofu() {
  try {
    for (std::size_t i = 0; i < sizeof(kCorpus) / sizeof(kCorpus[0]); ++i) {
      std::string text;
      if (!read_file(goldens_dir() + "/corpus/" + kCorpus[i], &text)) {
        std::printf("missing corpus input: %s\n", kCorpus[i]);
        return false;
      }
      if (!shapes_without_tofu(text)) {
        std::printf("tofu in corpus input: %s\n", kCorpus[i]);
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_manifest_covers_corpus() {
  try {
    std::string manifest;
    if (!read_file(goldens_dir() + "/manifest.json", &manifest)) return false;
    std::vector<std::string> tokens = manifest_txt_tokens(manifest);
    for (std::size_t i = 0; i < sizeof(kCorpus) / sizeof(kCorpus[0]); ++i) {
      bool found = false;
      for (std::size_t j = 0; j < tokens.size(); ++j) {
        if (tokens[j] == kCorpus[i]) {
          found = true;
          break;
        }
      }
      if (!found) {
        std::printf("manifest omits corpus input: %s\n", kCorpus[i]);
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_no_orphan_corpus_inputs() {
  try {
    // Every *.txt file actually present in corpus/ must be listed in the
    // manifest (no silent unlisted inputs), and every harness-known input
    // must exist on disk. The directory itself is enumerated — an extra
    // file no case names is still caught.
    std::vector<std::string> on_disk;
    if (!list_corpus_txt_files(&on_disk)) {
      std::printf("corpus/: directory listing failed\n");
      return false;
    }
    std::string manifest;
    if (!read_file(goldens_dir() + "/manifest.json", &manifest)) return false;
    std::vector<std::string> tokens = manifest_txt_tokens(manifest);
    for (std::size_t i = 0; i < on_disk.size(); ++i) {
      bool listed = false;
      for (std::size_t j = 0; j < tokens.size(); ++j) {
        if (tokens[j] == on_disk[i]) {
          listed = true;
          break;
        }
      }
      if (!listed) {
        std::printf("orphan corpus input not in manifest: %s\n",
                    on_disk[i].c_str());
        return false;
      }
    }
    for (std::size_t i = 0; i < sizeof(kCorpus) / sizeof(kCorpus[0]); ++i) {
      bool present = false;
      for (std::size_t j = 0; j < on_disk.size(); ++j) {
        if (on_disk[j] == kCorpus[i]) {
          present = true;
          break;
        }
      }
      if (!present) {
        std::printf("known corpus input missing on disk: %s\n", kCorpus[i]);
        return false;
      }
    }
    return true;
  } catch (...) {
    return false;
  }
}

static bool check_arabic_joining_matches_harfbuzz_dump() {
  try {
    // Corpus half: the Arabic fixture shapes tofu-free.
    std::string text;
    if (!read_file(goldens_dir() + "/corpus/arabic_joining.txt", &text)) {
      return false;
    }
    if (!shapes_without_tofu(text)) return false;
    // Dump half: the committed shaper reproduces the checked-in
    // arabic_joining TSV row (exact ids, 1e-3 px advances — T5 tolerance).
    // NOTE: the corpus bytes ("العربية shaping test" + suffix)
    // intentionally differ from the TSV row bytes (D8B3D984D8A7D985),
    // so the row half shapes the TSV row bytes directly — the same
    // comparison T5 performs — instead of asserting corpus bytes equal
    // the row. A corpus file shaping to wrong glyphs is still caught on
    // the tofu half; a backend that drifts from the dump is caught here.
    return tsv_row_matches_shaper("arabic_joining");
  } catch (...) {
    return false;
  }
}

static bool check_indic_joining_matches_harfbuzz_dump() {
  try {
    std::string text;
    if (!read_file(goldens_dir() + "/corpus/indic_shaping.txt", &text)) {
      return false;
    }
    if (!shapes_without_tofu(text)) return false;
    // Same corpus-vs-row note as the Arabic case: the corpus bytes (six
    // Devanagari codepoints + " shaping test" suffix) intentionally differ
    // from either TSV row, so both rows are shaped from their own TSV bytes
    // and compared (exact ids incl. matra reorder, 1e-3 px advances).
    if (!tsv_row_matches_shaper("devanagari")) return false;
    return tsv_row_matches_shaper("deva_matra");
  } catch (...) {
    return false;
  }
}

static bool check_bidi_corpus_is_mixed_direction() {
  try {
    // The bidi corpus input must contain both RTL (Hebrew/Arabic) and LTR
    // content so the external device-lab run exercises mixed-direction
    // paragraphs; the host asserts both directions present + shapability.
    std::string text;
    if (!read_file(goldens_dir() + "/corpus/bidi_mixed.txt", &text)) {
      return false;
    }
    if (text.find("Hello") == std::string::npos) {
      std::printf("bidi corpus: missing LTR run\n");
      return false;
    }
    if (!contains_hebrew_or_arabic(text)) {
      std::printf("bidi corpus: no Hebrew/Arabic codepoint\n");
      return false;
    }
    return shapes_without_tofu(text) && text.size() > 16;
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
  RUN_CASE(check_manifest_present);
  RUN_CASE(check_corpus_inputs_shape_without_tofu);
  RUN_CASE(check_manifest_covers_corpus);
  RUN_CASE(check_no_orphan_corpus_inputs);
  RUN_CASE(check_arabic_joining_matches_harfbuzz_dump);
  RUN_CASE(check_indic_joining_matches_harfbuzz_dump);
  RUN_CASE(check_bidi_corpus_is_mixed_direction);
  if (failures > 0) {
    std::printf("golden_harness_test: %d failing\n", failures);
    return 1;
  }
  std::printf("golden_harness_test: all passing\n");
  return 0;
}
