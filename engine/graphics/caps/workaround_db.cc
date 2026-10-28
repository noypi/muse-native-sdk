// Data-driven GPU workaround DB: file reload + match-only lookup.
//
// The DB file is JSON: {"version": <number>, "entries": [
//   {"vendor": ..., "driver_version": ..., "gpu_id": ...,
//    "feature": ..., "workaround_flag": ..., "engine_version": ...}, ...]}.
// `reload_db` parses and validates the file, then swaps the active entry set
// without recompiling. `match_workarounds` returns only the workaround flags
// whose vendor/driver/GPU/engine constraints all match the fingerprint.
// Self-contained C++17: minimal JSON scanning, no third-party dependency.
#include "caps/workaround_db.h"

#include <cctype>
#include <cstdio>
#include <mutex>
#include <utility>

namespace graphics {
namespace {

// DeviceFingerprint carries no engine version, so a non-empty
// `engine_version` constraint cannot be compared against the device; it is
// treated as documentation-only and always matches. Empty constraints of any
// kind are wildcards.
constexpr std::size_t kMaxDbBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaxFieldChars = 4096u;

struct DbEntry {
  std::string vendor;
  std::string driver_version;
  std::string gpu_id;
  std::string feature;
  std::string workaround_flag;
  std::string engine_version;
};

std::mutex g_mu;
std::vector<DbEntry> g_entries;
std::string g_version = "0";

[[noreturn]] void Fail() { throw DbSchemaMismatchError(); }

// Parses a JSON string literal at s[*i] (expects opening quote), handling the
// standard escapes. Advances *i past the closing quote.
std::string ParseJsonString(const std::string& s, std::size_t* i) {
  if (*i >= s.size() || s[*i] != '"') Fail();
  ++(*i);
  std::string out;
  while (*i < s.size()) {
    const char c = s[*i];
    if (c == '"') {
      ++(*i);
      return out;
    }
    if (c == '\\') {
      ++(*i);
      if (*i >= s.size()) Fail();
      const char e = s[*i];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        // \uXXXX: not needed for ASCII constraint values; skip the digits.
        case 'u': {
          if (*i + 4 >= s.size()) Fail();
          for (int k = 1; k <= 4; ++k) {
            if (!std::isxdigit(static_cast<unsigned char>(s[*i + k]))) Fail();
          }
          out.push_back('?');
          *i += 4;
          break;
        }
        default: Fail();
      }
      ++(*i);
      continue;
    }
    if (static_cast<unsigned char>(c) < 0x20) Fail();
    out.push_back(c);
    ++(*i);
    if (out.size() > kMaxFieldChars) Fail();
  }
  Fail();
}

void SkipWs(const std::string& s, std::size_t* i) {
  while (*i < s.size() &&
         (s[*i] == ' ' || s[*i] == '\t' || s[*i] == '\n' || s[*i] == '\r')) {
    ++(*i);
  }
}

// Finds the value span of top-level key `key` inside object text `obj`
// (including braces). Returns false when absent.
bool FindKey(const std::string& obj, const std::string& key,
             std::size_t* val_begin, std::size_t* val_end, bool* is_string) {
  const std::string quoted = "\"" + key + "\"";
  std::size_t pos = obj.find(quoted);
  if (pos == std::string::npos) return false;
  pos += quoted.size();
  std::size_t i = pos;
  SkipWs(obj, &i);
  if (i >= obj.size() || obj[i] != ':') Fail();
  ++i;
  SkipWs(obj, &i);
  if (i >= obj.size()) Fail();
  if (obj[i] == '"') {
    std::size_t j = i;
    (void)ParseJsonString(obj, &j);
    *val_begin = i;
    *val_end = j;
    *is_string = true;
    return true;
  }
  if (obj[i] == '[' || obj[i] == '{') {
    // Composite value: scan to the matching close bracket, respecting
    // string literals and nesting.
    const char open = obj[i];
    const char close = (open == '[') ? ']' : '}';
    std::size_t j = i + 1;
    int depth = 1;
    bool in_str = false;
    bool esc = false;
    for (; j < obj.size(); ++j) {
      const char c = obj[j];
      if (in_str) {
        if (esc) {
          esc = false;
        } else if (c == '\\') {
          esc = true;
        } else if (c == '"') {
          in_str = false;
        }
        continue;
      }
      if (c == '"') {
        in_str = true;
      } else if (c == open) {
        ++depth;
      } else if (c == close) {
        if (--depth == 0) {
          ++j;
          break;
        }
      }
    }
    if (depth != 0 || in_str) Fail();
    *val_begin = i;
    *val_end = j;
    *is_string = false;
    return true;
  }
  std::size_t j = i;
  while (j < obj.size() && obj[j] != ',' && obj[j] != '}' && obj[j] != ']' &&
         obj[j] != '"' && obj[j] != '{' && obj[j] != '[' &&
         !std::isspace(static_cast<unsigned char>(obj[j])) &&
         obj[j] != ':') {
    ++j;
  }
  if (j == i) Fail();
  *val_begin = i;
  *val_end = j;
  *is_string = false;
  return true;
}

std::string FieldOrEmpty(const std::string& obj, const std::string& key) {
  std::size_t b = 0, e = 0;
  bool is_string = false;
  if (!FindKey(obj, key, &b, &e, &is_string)) return "";
  if (!is_string) Fail();  // constraint fields must be JSON strings.
  std::size_t i = b;
  return ParseJsonString(obj, &i);
}

// Splits the raw text of the `entries` array (without brackets) into object
// spans by tracking brace depth and string literals.
std::vector<std::string> SplitObjects(const std::string& inner) {
  std::vector<std::string> out;
  std::size_t i = 0;
  while (i < inner.size()) {
    SkipWs(inner, &i);
    if (i >= inner.size()) break;
    if (inner[i] == ',') {
      ++i;
      continue;
    }
    if (inner[i] != '{') Fail();
    std::size_t start = i;
    int depth = 0;
    bool in_str = false;
    bool esc = false;
    for (; i < inner.size(); ++i) {
      const char c = inner[i];
      if (in_str) {
        if (esc) {
          esc = false;
        } else if (c == '\\') {
          esc = true;
        } else if (c == '"') {
          in_str = false;
        }
        continue;
      }
      if (c == '"') {
        in_str = true;
      } else if (c == '{') {
        ++depth;
      } else if (c == '}') {
        --depth;
        if (depth == 0) {
          ++i;
          break;
        }
      }
    }
    if (depth != 0 || in_str) Fail();
    out.push_back(inner.substr(start, i - start));
  }
  return out;
}

bool Matches(const DbEntry& e, const DeviceFingerprint& f) {
  if (e.workaround_flag.empty()) return false;
  if (!e.vendor.empty() && e.vendor != f.vendor) return false;
  if (!e.driver_version.empty() && e.driver_version != f.driver_version)
    return false;
  if (!e.gpu_id.empty() && e.gpu_id != f.gpu_id) return false;
  // engine_version + feature are pipeline descriptors, not device probes:
  // they scope the workaround but cannot mismatch a fingerprint.
  return true;
}

std::string ReadFile(const std::string& path) {
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) Fail();
  std::string data;
  char buf[4096];
  std::size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), fp)) > 0) {
    data.append(buf, n);
    if (data.size() > kMaxDbBytes) {
      std::fclose(fp);
      Fail();
    }
  }
  const bool read_error = std::ferror(fp) != 0;
  std::fclose(fp);
  if (read_error || data.empty()) Fail();
  return data;
}

}  // namespace

DbVersion reload_db(const std::string& path) {
  const std::string text = ReadFile(path);
  std::size_t b = 0, e = 0;
  bool is_string = false;
  if (!FindKey(text, "version", &b, &e, &is_string)) Fail();
  std::string version;
  if (is_string) {
    Fail();  // version must be a schema number, e.g. {"version": 1}.
  } else {
    version = text.substr(b, e - b);
    if (version.empty()) Fail();
    for (const char c : version) {
      if (!std::isdigit(static_cast<unsigned char>(c))) Fail();
    }
    // Canonicalize: strip leading zeros / whitespace remnants.
    std::size_t k = 0;
    while (k + 1 < version.size() && version[k] == '0') ++k;
    version = version.substr(k);
  }

  std::vector<DbEntry> entries;
  if (FindKey(text, "entries", &b, &e, &is_string)) {
    if (is_string) Fail();
    std::string raw = text.substr(b, e - b);
    std::size_t i = 0;
    SkipWs(raw, &i);
    if (i >= raw.size() || raw[i] != '[') Fail();
    ++i;
    // Find matching close bracket for the entries array.
    int depth = 1;
    bool in_str = false;
    bool esc = false;
    std::size_t inner_begin = i;
    std::size_t inner_end = std::string::npos;
    for (; i < raw.size(); ++i) {
      const char c = raw[i];
      if (in_str) {
        if (esc) {
          esc = false;
        } else if (c == '\\') {
          esc = true;
        } else if (c == '"') {
          in_str = false;
        }
        continue;
      }
      if (c == '"') {
        in_str = true;
      } else if (c == '[') {
        ++depth;
      } else if (c == ']') {
        if (--depth == 0) {
          inner_end = i;
          break;
        }
      }
    }
    if (inner_end == std::string::npos) Fail();
    for (const std::string& obj : SplitObjects(raw.substr(
             inner_begin, inner_end - inner_begin))) {
      DbEntry entry;
      entry.vendor = FieldOrEmpty(obj, "vendor");
      entry.driver_version = FieldOrEmpty(obj, "driver_version");
      entry.gpu_id = FieldOrEmpty(obj, "gpu_id");
      entry.feature = FieldOrEmpty(obj, "feature");
      entry.workaround_flag = FieldOrEmpty(obj, "workaround_flag");
      entry.engine_version = FieldOrEmpty(obj, "engine_version");
      entries.push_back(std::move(entry));
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_mu);
    g_entries = std::move(entries);
    g_version = version;
  }
  DbVersion v;
  v.version = version;
  return v;
}

std::vector<std::string> match_workarounds(
    const DeviceFingerprint& fingerprint) {
  std::lock_guard<std::mutex> lock(g_mu);
  std::vector<std::string> out;
  for (const DbEntry& entry : g_entries) {
    if (Matches(entry, fingerprint)) out.push_back(entry.workaround_flag);
  }
  return out;
}

}  // namespace graphics
