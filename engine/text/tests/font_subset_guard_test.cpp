// T7 — AC-TEXT-027 / FR-TEXT-011: font-subset guard — shipped locales +
// emoji glyphs are never stripped.
// Host harness (no test framework): static bool check_*() cases + main,
// return 0 all-green / non-zero + failing name. Shape mirrors
// engine/text/tests/font_fallback_test.cpp (Batch 1 T1).
// The guard entry point itself (toolchain/font-subset/main.cpp) is owned by
// the coder; this harness verifies (a) the keep-list data file parses and
// covers every required range class, and (b) the guard entry point exists
// with the specified-but-unrunnable-here Gradle wiring. Gradle
// (./gradlew :toolchain:fontSubset) cannot execute in this environment (no
// wrapper/toolchain downloads), so the hermetic proof is this harness;
// the Gradle wiring is recorded as specified-but-unrunnable-here.
// NOTE: engine interface is `none — build-tool guard entry, no callable
// engine API` (plan Interface line), so this harness drives NO production
// symbol: it reads the keep-list as DATA from the source tree relative to
// __FILE__. Expected RED: subset_keep_list.txt and
// toolchain/font-subset/main.cpp do not exist yet, so the file-presence
// and coverage cases fail — missing behavior/data, not a malformed test.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// Keep-list line format (subset_keep_list.txt):
//   U+0041            (single codepoint)
//   U+1F3FB-U+1F3FF   (inclusive range)
//   # comment / blank lines ignored; trailing '#' starts a comment.
namespace {

std::string source_dir() {
  std::string file = __FILE__;
  std::string::size_type slash = file.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return file.substr(0, slash);
}

bool parse_hex(const std::string& s, std::size_t pos, std::size_t len,
               uint32_t* out) {
  if (len == 0 || len > 6 || pos + len > s.size()) return false;
  uint32_t v = 0;
  for (std::size_t i = 0; i < len; ++i) {
    char c = s[pos + i];
    v <<= 4;
    if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
    else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
    else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
    else return false;
  }
  if (v > 0x10FFFFu) return false;
  *out = v;
  return true;
}

struct KeepRange {
  uint32_t lo = 0;
  uint32_t hi = 0;
};

bool parse_keep_line(const std::string& line, KeepRange* out) {
  std::string t = line;
  std::string::size_type hash = t.find('#');
  if (hash != std::string::npos) t = t.substr(0, hash);
  std::size_t b = t.find_first_not_of(" \t\r");
  if (b == std::string::npos) return false;  // blank: caller skips
  std::size_t e = t.find_last_not_of(" \t\r");
  t = t.substr(b, e - b + 1);
  if (t.size() < 3 || t[0] != 'U' || t[1] != '+') return false;
  uint32_t lo = 0, hi = 0;
  std::size_t i = 2;
  std::size_t j = i;
  while (j < t.size() && t[j] != '-') ++j;
  if (!parse_hex(t, i, j - i, &lo)) return false;
  if (j == t.size()) {
    hi = lo;
  } else {
    std::string::size_type k = j + 1;
    if (k + 1 >= t.size() || t[k] != 'U' || t[k + 1] != '+') return false;
    if (!parse_hex(t, k + 2, t.size() - k - 2, &hi)) return false;
    if (hi < lo) return false;
  }
  out->lo = lo;
  out->hi = hi;
  return true;
}

bool load_keep_list(const std::string& path, std::vector<KeepRange>* out) {
  out->clear();
  std::ifstream in(path);
  if (!in.is_open()) return false;
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
    std::string::size_type b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) continue;  // blank
    if (line[b] == '#') continue;          // comment
    KeepRange r{};
    if (!parse_keep_line(line, &r)) return false;
    out->push_back(r);
  }
  return !out->empty();
}

std::string keep_list_path() {
  return source_dir() + "/data/subset_keep_list.txt";
}

bool covers(const std::vector<KeepRange>& ranges, uint32_t cp) {
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    if (ranges[i].lo <= cp && cp <= ranges[i].hi) return true;
  }
  return false;
}

bool covers_all(const std::vector<KeepRange>& ranges, uint32_t lo,
                uint32_t hi) {
  for (uint32_t cp = lo; cp <= hi; ++cp) {
    if (!covers(ranges, cp)) return false;
    if (cp == hi) break;
  }
  return true;
}

bool file_exists(const std::string& path) {
  std::ifstream in(path);
  return in.is_open();
}

}  // namespace

static bool check_keep_list_present_and_parses() {
  try {
    std::vector<KeepRange> ranges;
    return load_keep_list(keep_list_path(), &ranges);
  } catch (...) {
    return false;
  }
}

static bool check_shipped_locales_covered() {
  try {
    std::vector<KeepRange> ranges;
    if (!load_keep_list(keep_list_path(), &ranges)) return false;
    // One representative per shipped locale/script set (T1/T3 matrix):
    // Latin, CJK, Arabic, Hebrew, Devanagari, Thai, Khmer, Myanmar.
    return covers(ranges, 0x0041) &&   // Latin A (en-US)
           covers(ranges, 0x4E2D) &&   // CJK U+4E2D (zh-Hans-CN)
           covers(ranges, 0x0627) &&   // Arabic alef (ar-EG)
           covers(ranges, 0x05D0) &&   // Hebrew alef (he-IL)
           covers(ranges, 0x0905) &&   // Devanagari (hi-IN)
           covers(ranges, 0x0E01) &&   // Thai (th-TH)
           covers(ranges, 0x1780) &&   // Khmer (km-KH)
           covers(ranges, 0x1000);     // Myanmar (my-MM)
  } catch (...) {
    return false;
  }
}

static bool check_zwj_kept() {
  try {
    std::vector<KeepRange> ranges;
    if (!load_keep_list(keep_list_path(), &ranges)) return false;
    return covers(ranges, 0x200D);  // ZERO WIDTH JOINER
  } catch (...) {
    return false;
  }
}

static bool check_skin_tones_kept() {
  try {
    std::vector<KeepRange> ranges;
    if (!load_keep_list(keep_list_path(), &ranges)) return false;
    return covers_all(ranges, 0x1F3FB, 0x1F3FF);  // light..dark
  } catch (...) {
    return false;
  }
}

static bool check_regional_indicators_kept() {
  try {
    std::vector<KeepRange> ranges;
    if (!load_keep_list(keep_list_path(), &ranges)) return false;
    return covers_all(ranges, 0x1F1E6, 0x1F1FF);  // RI A..Z (flags)
  } catch (...) {
    return false;
  }
}

static bool check_keycap_kept() {
  try {
    std::vector<KeepRange> ranges;
    if (!load_keep_list(keep_list_path(), &ranges)) return false;
    return covers(ranges, 0x20E3) &&  // COMBINING ENCLOSING KEYCAP
           covers(ranges, 0xFE0F);    // VARIATION SELECTOR-16
  } catch (...) {
    return false;
  }
}

static bool check_malformed_line_rejected() {
  try {
    KeepRange r{};
    // Parser itself must reject garbage: reversed range, bad hex, >max.
    if (parse_keep_line("U+1F3FF-U+1F3FB", &r)) return false;
    if (parse_keep_line("U+ZZZZ", &r)) return false;
    if (parse_keep_line("U+110000", &r)) return false;
    if (parse_keep_line("1F3FB", &r)) return false;
    // Valid lines still accepted (format is not over-strict).
    if (!parse_keep_line("U+200D", &r) || r.lo != 0x200Du) return false;
    if (!parse_keep_line("U+1F3FB-U+1F3FF # comment", &r)) return false;
    return r.lo == 0x1F3FBu && r.hi == 0x1F3FFu;
  } catch (...) {
    return false;
  }
}

static bool check_parse_hex_numeric_boundaries() {
  try {
    KeepRange r{};
    // >6 hex digits rejected (fail-closed: never grant coverage).
    if (parse_keep_line("U+1234567", &r)) return false;
    if (parse_keep_line("U+0041-U+1234567", &r)) return false;
    // >0x10FFFF rejected (max valid codepoint is U+10FFFF).
    if (parse_keep_line("U+110000", &r)) return false;
    if (parse_keep_line("U+0041-U+110000", &r)) return false;
    // hi<lo rejected (fail-closed: reversed range grants nothing).
    if (parse_keep_line("U+1F3FF-U+1F3FB", &r)) return false;
    if (parse_keep_line("U+0042-U+0041", &r)) return false;
    // Valid boundaries still accepted (not over-strict).
    if (!parse_keep_line("U+10FFFF", &r) || r.lo != 0x10FFFFu) return false;
    if (!parse_keep_line("U+0041-U+0041", &r)) return false;
    return r.lo == 0x41u && r.hi == 0x41u;
  } catch (...) {
    return false;
  }
};

static bool check_guard_entry_present() {
  try {
    // The build-tool guard entry the coder owns. Path is relative to this
    // file: engine/text/tests -> toolchain/font-subset/main.cpp.
    return file_exists(source_dir() +
                       "/../../../toolchain/font-subset/main.cpp");
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
  RUN_CASE(check_keep_list_present_and_parses);
  RUN_CASE(check_shipped_locales_covered);
  RUN_CASE(check_zwj_kept);
  RUN_CASE(check_skin_tones_kept);
  RUN_CASE(check_regional_indicators_kept);
  RUN_CASE(check_keycap_kept);
  RUN_CASE(check_malformed_line_rejected);
  RUN_CASE(check_parse_hex_numeric_boundaries);
  RUN_CASE(check_guard_entry_present);
  if (failures > 0) {
    std::printf("font_subset_guard_test: %d failing\n", failures);
    return 1;
  }
  std::printf("font_subset_guard_test: all passing\n");
  return 0;
}
