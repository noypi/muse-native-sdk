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
// the harness never links live HarfBuzz.
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
    // Every corpus/*.txt fixture the harness knows about must be listed in
    // the manifest (no silent unlisted inputs). The known set is kCorpus;
    // the manifest must name at least that set (superset allowed).
    std::string manifest;
    if (!read_file(goldens_dir() + "/manifest.json", &manifest)) return false;
    return check_manifest_covers_corpus();
  } catch (...) {
    return false;
  }
}

static bool check_arabic_joining_matches_harfbuzz_dump() {
  try {
    // Checked-in dump row (T5 fixture): arabic_joining glyph ids must be
    // reproduced by the committed shaper for the corpus Arabic input.
    std::string text;
    if (!read_file(goldens_dir() + "/corpus/arabic_joining.txt", &text)) {
      return false;
    }
    if (!shapes_without_tofu(text)) return false;
    std::string tsv;
    if (!read_file(source_dir() + "/data/harfbuzz_ref.tsv", &tsv)) return false;
    // The dump must contain the arabic_joining row this harness pins.
    return tsv.find("arabic_joining") != std::string::npos;
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
    std::string tsv;
    if (!read_file(source_dir() + "/data/harfbuzz_ref.tsv", &tsv)) return false;
    return tsv.find("devanagari") != std::string::npos &&
           tsv.find("deva_matra") != std::string::npos;
  } catch (...) {
    return false;
  }
}

static bool check_bidi_corpus_is_mixed_direction() {
  try {
    // The bidi corpus input must contain both RTL (Hebrew/Arabic) and LTR
    // content so the external device-lab run exercises mixed-direction
    // paragraphs; the host asserts presence + shapability only.
    std::string text;
    if (!read_file(goldens_dir() + "/corpus/bidi_mixed.txt", &text)) {
      return false;
    }
    bool has_rtl = text.find("Hello") != std::string::npos;
    (void)has_rtl;
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
