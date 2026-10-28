// T7 — AC-TEXT-027 / FR-TEXT-011: font-subset guard entry point.
//
// Minimal keep-list checker only (not a full subsetter). Exits 0 iff every
// codepoint in subset_keep_list.txt (shipped locales + emoji ranges incl.
// ZWJ/skin-tone/RI/keycap) survives the subset plan; exits 2 + offending
// range list otherwise.
//
// Usage:
//   font_subset_guard [keep_list] [plan]
//
//   keep_list defaults to engine/text/tests/data/subset_keep_list.txt
//   relative to the working directory. plan is an optional file in the
//   same line format listing the ranges the subset plan keeps; when given,
//   every keep-list range must be covered by the plan. With no plan, the
//   guard verifies the keep-list itself parses (fail-closed: a missing or
//   malformed keep-list exits 2, never 0).
//
// Keep-list line format (matches engine/text/tests/font_subset_guard_test.cpp):
//   U+0041              (single codepoint)
//   U+1F3FB-U+1F3FF     (inclusive range)
//   '#' starts a comment; blank lines are ignored.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

struct KeepRange {
  uint32_t lo = 0;
  uint32_t hi = 0;
};

bool ParseHex(const std::string& s, std::size_t pos, std::size_t len,
              uint32_t* out) {
  if (len == 0 || len > 6 || pos + len > s.size()) return false;
  uint32_t v = 0;
  for (std::size_t i = 0; i < len; ++i) {
    char c = s[pos + i];
    v <<= 4;
    if (c >= '0' && c <= '9') {
      v |= static_cast<uint32_t>(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      v |= static_cast<uint32_t>(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
      v |= static_cast<uint32_t>(c - 'a' + 10);
    } else {
      return false;
    }
  }
  if (v > 0x10FFFFu) return false;
  *out = v;
  return true;
}

// Parses one non-blank, non-comment line. Returns false for malformed lines
// (missing U+ prefix, bad hex, out-of-range value, reversed range).
bool ParseKeepLine(const std::string& line, KeepRange* out) {
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
  if (!ParseHex(t, i, j - i, &lo)) return false;
  if (j == t.size()) {
    hi = lo;
  } else {
    std::string::size_type k = j + 1;
    if (k + 1 >= t.size() || t[k] != 'U' || t[k + 1] != '+') return false;
    if (!ParseHex(t, k + 2, t.size() - k - 2, &hi)) return false;
    if (hi < lo) return false;
  }
  out->lo = lo;
  out->hi = hi;
  return true;
}

bool LoadKeepList(const std::string& path, std::vector<KeepRange>* out,
                  std::string* error) {
  out->clear();
  std::ifstream in(path);
  if (!in.is_open()) {
    *error = "cannot open " + path;
    return false;
  }
  std::string line;
  std::size_t lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    if (!line.empty() && line[line.size() - 1] == '\r') line.pop_back();
    std::string::size_type b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) continue;  // blank
    if (line[b] == '#') continue;          // comment
    KeepRange r{};
    if (!ParseKeepLine(line, &r)) {
      *error = path + ":" + std::to_string(lineno) +
               ": malformed keep-list line: " + line;
      return false;
    }
    out->push_back(r);
  }
  if (out->empty()) {
    *error = path + ": empty keep-list (no ranges)";
    return false;
  }
  return true;
}

bool Covers(const std::vector<KeepRange>& ranges, uint32_t cp) {
  for (std::size_t i = 0; i < ranges.size(); ++i) {
    if (ranges[i].lo <= cp && cp <= ranges[i].hi) return true;
  }
  return false;
}

std::string FormatRange(const KeepRange& r) {
  char buf[32];
  if (r.lo == r.hi) {
    std::snprintf(buf, sizeof(buf), "U+%04X",
                  static_cast<unsigned int>(r.lo));
  } else {
    std::snprintf(buf, sizeof(buf), "U+%04X-U+%04X",
                  static_cast<unsigned int>(r.lo),
                  static_cast<unsigned int>(r.hi));
  }
  return buf;
}

}  // namespace

// Canonical guard entry for the `:toolchain:fontSubset` task (NFR-BUILD-017 /
// AC-BUILD-063, TC-BUILD-063). Hermetic gate: toolchain/assets/fontSubsetGate.js.
int main(int argc, char** argv) {
  if (argc > 3) {
    std::fprintf(stderr, "usage: %s [keep_list] [plan]\n",
                 argc > 0 ? argv[0] : "font_subset_guard");
    return 2;
  }
  const std::string keep_path =
      argc >= 2 ? argv[1] : "engine/text/tests/data/subset_keep_list.txt";

  std::vector<KeepRange> keep;
  std::string error;
  if (!LoadKeepList(keep_path, &keep, &error)) {
    std::fprintf(stderr, "font-subset guard FAILED: %s\n", error.c_str());
    return 2;
  }

  if (argc < 3) {
    std::printf("font-subset guard OK: %zu keep range(s) from %s\n",
                keep.size(), keep_path.c_str());
    return 0;
  }

  std::vector<KeepRange> plan;
  if (!LoadKeepList(argv[2], &plan, &error)) {
    std::fprintf(stderr, "font-subset guard FAILED: %s\n", error.c_str());
    return 2;
  }

  // Every keep-list range must be fully covered by the plan. Report each
  // keep range that loses at least one codepoint.
  std::vector<KeepRange> stripped;
  for (std::size_t i = 0; i < keep.size(); ++i) {
    for (uint32_t cp = keep[i].lo;; ++cp) {
      if (!Covers(plan, cp)) {
        stripped.push_back(keep[i]);
        break;
      }
      if (cp == keep[i].hi) break;
    }
  }
  if (!stripped.empty()) {
    std::fprintf(stderr,
                 "font-subset guard FAILED: %zu keep range(s) stripped "
                 "by the subset plan:\n",
                 stripped.size());
    for (std::size_t i = 0; i < stripped.size(); ++i) {
      std::fprintf(stderr, "  %s\n", FormatRange(stripped[i]).c_str());
    }
    return 2;
  }

  std::printf("font-subset guard OK: all %zu keep range(s) survive %s\n",
              keep.size(), argv[2]);
  return 0;
}
