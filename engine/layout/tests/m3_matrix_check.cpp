// T9 contract test — FR-LAYOUT-001 / AC-LAYOUT-004, TC-LAYOUT-004 file check.
//
// Validates `engine/layout/m3_matrix.json` at test time WITHOUT a JSON
// library (no vendoring, per RULE-DEPS-005): a dependency-free C++17
// structural string scan with string-literal awareness (handles \" escapes).
//
// Interface (plan T9 governs): file check over the top-level object
// `{ "schemaVersion": 1, "tokenSnapshot": "<date>", "components": [...] }`;
// every entry `status == "present"` (zero `"missing"`); exit code 0 iff the
// schema holds and zero entries are missing.
//
// Hermetic: C++17 + STL only; reads ONLY the JSON path given as argv[1]
// (no network, no env/config, no secrets, no live services).
//
// Usage: m3_matrix_check <path/to/m3_matrix.json>  → 0 on success, 1 on
// schema/content failure, 2 on usage/IO failure.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Stage 4 security bounds (fail-closed, never crash/hang): documents
// larger than this are rejected before scanning, and value nesting deeper
// than this is rejected during scanning. The committed matrix is tens of
// KB with depth < 8, so neither bound affects legitimate input.
constexpr std::size_t kMaxDocBytes = 1u << 20;  // 1 MiB
constexpr int kMaxNestDepth = 64;

// Required component states (AC-LAYOUT-025: consistent across the set).
const char* const kRequiredStates[] = {
    "enabled", "hovered", "focused", "pressed", "dragged", "disabled",
    "error",
};

// Required per-component string fields (non-empty), besides tokens/states.
const char* const kRequiredFields[] = {
    "name", "primitive", "semantics", "goldenTest",
};

// Skips ASCII whitespace. Returns false at end of input.
bool skip_ws(const std::string& s, std::size_t& i) {
  while (i < s.size() &&
         (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
    ++i;
  }
  return i < s.size();
}

// Parses a JSON string literal at s[i] (s[i] must be '"') into out,
// honouring backslash escapes. Advances i past the closing quote.
bool parse_string(const std::string& s, std::size_t& i, std::string& out) {
  if (i >= s.size() || s[i] != '"') {
    return false;
  }
  ++i;
  out.clear();
  while (i < s.size()) {
    const char c = s[i];
    if (c == '\\') {
      if (i + 1 >= s.size()) {
        return false;
      }
      out.push_back(s[i + 1]);
      i += 2;
      continue;
    }
    if (c == '"') {
      ++i;
      return true;
    }
    out.push_back(c);
    ++i;
  }
  return false;
}

// Parses a JSON value starting at s[i], returning the raw substring and the
// index one past its end. Handles nested {} [] and string literals.
bool take_value(const std::string& s, std::size_t i, std::size_t& end) {
  if (!skip_ws(s, i)) {
    return false;
  }
  if (s[i] == '"') {
    std::string ignored;
    if (!parse_string(s, i, ignored)) {
      return false;
    }
    end = i;
    return true;
  }
  if (s[i] == '{' || s[i] == '[') {
    int depth_brace = 0;
    int depth_bracket = 0;
    int depth = 0;  // Combined nesting depth, bounded by kMaxNestDepth.
    while (i < s.size()) {
      const char c = s[i];
      if (c == '"') {
        std::string ignored;
        if (!parse_string(s, i, ignored)) {
          return false;
        }
        continue;
      }
      if (c == '{') {
        ++depth_brace;
        ++depth;
      } else if (c == '}') {
        --depth_brace;
        --depth;
      } else if (c == '[') {
        ++depth_bracket;
        ++depth;
      } else if (c == ']') {
        --depth_bracket;
        --depth;
      }
      if (depth > kMaxNestDepth) {
        return false;  // Fail closed on pathological nesting (no hang).
      }
      ++i;
      if (depth_brace == 0 && depth_bracket == 0) {
        end = i;
        return true;
      }
      if (depth_brace < 0 || depth_bracket < 0) {
        return false;
      }
    }
    return false;
  }
  // Number / true / false / null: scan to the next structural character.
  const std::size_t start = i;
  while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
         s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') {
    ++i;
  }
  end = i;
  return end > start;
}

// Extracts the raw (still-encoded) value substring for key `key` inside the
// object spanning s[obj_begin, obj_end). Returns false when absent.
bool find_key_raw(const std::string& s, std::size_t obj_begin,
                  std::size_t obj_end, const std::string& key,
                  std::string& raw_out) {
  std::size_t i = obj_begin;
  if (!skip_ws(s, i) || s[i] != '{') {
    return false;
  }
  ++i;
  while (true) {
    if (!skip_ws(s, i) || i >= obj_end) {
      return false;
    }
    if (s[i] == '}') {
      return false;
    }
    std::string k;
    if (!parse_string(s, i, k)) {
      return false;
    }
    if (!skip_ws(s, i) || s[i] != ':') {
      return false;
    }
    ++i;
    std::size_t vend = 0;
    if (!take_value(s, i, vend)) {
      return false;
    }
    if (k == key) {
      raw_out = s.substr(i, vend - i);
      return true;
    }
    i = vend;
    if (!skip_ws(s, i)) {
      return false;
    }
    if (s[i] == ',') {
      ++i;
      continue;
    }
    if (s[i] == '}') {
      return false;
    }
    return false;
  }
}

// Splits the elements of the array spanning s (already trimmed) into raw
// substrings. Returns false when s is not an array.
bool split_array(const std::string& s, std::vector<std::string>& elems) {
  elems.clear();
  std::size_t i = 0;
  if (!skip_ws(s, i) || s[i] != '[') {
    return false;
  }
  ++i;
  if (!skip_ws(s, i)) {
    return false;
  }
  if (s[i] == ']') {
    return true;  // Empty array.
  }
  while (true) {
    std::size_t vend = 0;
    if (!take_value(s, i, vend)) {
      return false;
    }
    elems.push_back(s.substr(i, vend - i));
    i = vend;
    if (!skip_ws(s, i)) {
      return false;
    }
    if (s[i] == ',') {
      ++i;
      continue;
    }
    if (s[i] == ']') {
      return true;
    }
    return false;
  }
}

// Splits the member objects of the array spanning s into (begin, end)
// index ranges over s. Returns false when s is not an array of objects.
bool split_object_array(const std::string& s,
                        std::vector<std::pair<std::size_t, std::size_t>>& objs,
                        std::size_t base_offset) {
  objs.clear();
  std::size_t i = 0;
  if (!skip_ws(s, i) || s[i] != '[') {
    return false;
  }
  ++i;
  if (!skip_ws(s, i)) {
    return false;
  }
  if (s[i] == ']') {
    return true;
  }
  while (true) {
    std::size_t vend = 0;
    if (!take_value(s, i, vend)) {
      return false;
    }
    objs.push_back({base_offset + i, base_offset + vend});
    i = vend;
    if (!skip_ws(s, i)) {
      return false;
    }
    if (s[i] == ',') {
      ++i;
      continue;
    }
    if (s[i] == ']') {
      return true;
    }
    return false;
  }
}

std::string trim(const std::string& s) {
  std::size_t b = 0;
  while (b < s.size() &&
         (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) {
    ++b;
  }
  std::size_t e = s.size();
  while (e > b &&
         (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' ||
          s[e - 1] == '\r')) {
    --e;
  }
  return s.substr(b, e - b);
}

// Unquotes a raw JSON string value into out. Returns false when not a
// quoted string literal.
bool unquote(const std::string& raw, std::string& out) {
  const std::string t = trim(raw);
  std::size_t i = 0;
  return parse_string(t, i, out) && trim(t.substr(i)).empty();
}

int failures = 0;
bool g_verbose = true;  // Cleared by the adversarial self-pins for quiet runs.

void check(bool ok, const std::string& label) {
  if (g_verbose) {
    if (ok) {
      std::printf("PASS %s\n", label.c_str());
    } else {
      std::printf("FAIL %s\n", label.c_str());
    }
  }
  if (!ok) {
    ++failures;
  }
}

}  // namespace

// Size gate for the document layer: oversized input is rejected before
// scanning (fail-closed). The committed matrix is tens of KB.
bool docSizeOk(const std::string& doc) { return doc.size() <= kMaxDocBytes; }

// Validates one in-memory document; returns the number of failing checks
// (0 = valid). Honors ::g_verbose for diagnostic output. Shared by the
// argv-driven file check and the adversarial self-pins below.
int validateDocument(const std::string& doc) {
  failures = 0;

  // Top-level object.
  std::size_t t = 0;
  check(skip_ws(doc, t) && t < doc.size() && doc[t] == '{', "top-level object");
  if (failures > 0) {
    if (g_verbose) std::printf("m3_matrix_check: %d failing\n", failures);
    return failures;
  }

  // schemaVersion == 1.
  std::string raw;
  bool has_schema = find_key_raw(doc, 0, doc.size(), "schemaVersion", raw);
  check(has_schema, "schemaVersion present");
  check(has_schema && trim(raw) == "1", "schemaVersion == 1");

  // tokenSnapshot non-empty string (Expressive token date, plan A4).
  std::string snapshot_raw;
  bool has_snapshot =
      find_key_raw(doc, 0, doc.size(), "tokenSnapshot", snapshot_raw);
  std::string snapshot;
  check(has_snapshot && unquote(snapshot_raw, snapshot) && !snapshot.empty(),
        "tokenSnapshot present and non-empty");

  // components array.
  std::string comps_raw;
  bool has_comps = find_key_raw(doc, 0, doc.size(), "components", comps_raw);
  check(has_comps, "components present");
  std::vector<std::pair<std::size_t, std::size_t>> objs;
  // Offsets for split_object_array are relative to comps_raw; rebase by
  // locating comps_raw inside doc.
  const std::size_t comps_off = doc.find(comps_raw);
  const bool comps_ok =
      has_comps && split_object_array(comps_raw, objs, comps_off);
  check(comps_ok, "components is an array");
  check(comps_ok && !objs.empty(), "components non-empty");
  if (!comps_ok || objs.empty()) {
    if (g_verbose) std::printf("m3_matrix_check: %d failing\n", failures);
    return failures;
  }
  if (g_verbose) std::printf("PASS component count %u\n", (unsigned)objs.size());

  int missing = 0;
  for (std::size_t n = 0; n < objs.size(); ++n) {
    const std::size_t b = objs[n].first;
    const std::size_t e = objs[n].second;
    const std::string tag = "component[" + std::to_string(n) + "]";

    // Required string fields.
    for (const char* f : kRequiredFields) {
      std::string fr;
      std::string u;
      const bool ok =
          find_key_raw(doc, b, e, f, fr) && unquote(fr, u) && !u.empty();
      check(ok, tag + "." + f + " present and non-empty");
    }

    // tokens: non-empty array of non-empty strings.
    std::string toks;
    bool toks_ok = find_key_raw(doc, b, e, "tokens", toks);
    std::vector<std::string> tok_elems;
    toks_ok = toks_ok && split_array(toks, tok_elems) && !tok_elems.empty();
    if (toks_ok) {
      for (const auto& te : tok_elems) {
        std::string u;
        toks_ok = toks_ok && unquote(te, u) && !u.empty();
      }
    }
    check(toks_ok, tag + ".tokens non-empty");

    // states: array containing every required state.
    std::string states;
    bool states_ok = find_key_raw(doc, b, e, "states", states);
    std::vector<std::string> state_elems;
    states_ok = states_ok && split_array(states, state_elems);
    if (states_ok) {
      for (const char* need : kRequiredStates) {
        bool found = false;
        for (const auto& se : state_elems) {
          std::string u;
          if (unquote(se, u) && u == need) {
            found = true;
            break;
          }
        }
        if (!found) {
          states_ok = false;
          break;
        }
      }
    }
    check(states_ok, tag + ".states covers all 7 required states");

    // adaptive: boolean literal.
    std::string adapt;
    const bool adapt_ok = find_key_raw(doc, b, e, "adaptive", adapt) &&
                          (trim(adapt) == "true" || trim(adapt) == "false");
    check(adapt_ok, tag + ".adaptive is boolean");

    // status: must be "present"; anything else (incl. "missing") fails.
    std::string st;
    std::string status_val;
    const bool has_status = find_key_raw(doc, b, e, "status", st) &&
                            unquote(st, status_val);
    const bool status_ok =
        has_status && unquote(st, status_val) && status_val == "present";
    if (has_status && status_val != "present") {
      ++missing;
    }
    if (!has_status) {
      ++missing;
    }
    check(status_ok, tag + ".status == present");
  }
  check(missing == 0, "zero missing entries");

  return failures;
}

int runValidator(int argc, char** argv);

// Stage 4 security self-pins: adversarial inputs through the same
// `validateDocument` (plus argv handling through `runValidator`). Each
// must fail closed — nonzero rejection, no crash, no hang. Runs
// in-process on synthetic strings: no temp files, no network, no env.
bool runAdversarialPins() {
  bool ok = true;
  auto expectReject = [&ok](const std::string& doc, const char* label) {
    g_verbose = false;
    const int rc = validateDocument(doc);
    g_verbose = true;
    if (rc == 0) {
      std::printf("FAIL adversarial pin accepted: %s\n", label);
      ok = false;
    } else {
      std::printf("PASS adversarial pin rejected: %s\n", label);
    }
  };

  // Truncated JSON (cut mid-array/object, unterminated string).
  expectReject(
      "{\"schemaVersion\": 1, \"tokenSnapshot\": \"2026-10-28\", "
      "\"components\": [{\"name\": \"View\"",
      "truncated-mid-array");
  expectReject("{\"schemaVersion\": 1, \"tokenSnapshot\": \"abc",
               "truncated-mid-string");
  // Empty input (also guards the old top-level subscript shape).
  expectReject("", "empty-input");
  // Wrong top-level types.
  expectReject(
      "{\"schemaVersion\": \"1\", \"tokenSnapshot\": 7, "
      "\"components\": {\"name\": \"View\"}}",
      "wrong-top-level-types");
  // Component with wrong member types (tokens not an array, states
  // incomplete, adaptive not a bool).
  expectReject(
      "{\"schemaVersion\": 1, \"tokenSnapshot\": \"d\", \"components\": "
      "[{\"name\": \"N\", \"primitive\": \"P\", \"tokens\": \"t\", "
      "\"states\": [\"enabled\"], \"adaptive\": \"yes\", "
      "\"semantics\": \"S\", \"goldenTest\": \"G\", "
      "\"status\": \"present\"}]}",
      "wrong-member-types");
  // Regressed flat-map shape (BUG-LAYOUT-001): a component-keyed map with
  // no envelope keys (`schemaVersion` / `tokenSnapshot` / `components`) and
  // per-entry `golden` / `ledgers` / `motionTokens` instead of the T9
  // `goldenTest` + 7-state + `adaptive` + `status` entry contract. Must stay
  // red by construction — the gate never accepts the envelope-less shape.
  expectReject(
      "{\"Button\": {\"primitive\": \"M3NativeButton\", "
      "\"tokens\": [\"md.comp.button.container\"], "
      "\"motionTokens\": [\"motion.durations.short-2\"], "
      "\"semantics\": \"S\", \"golden\": \"G\", "
      "\"ledgers\": {\"clone\": \"C\", \"competitive\": \"K\"}}}",
      "flat-map-no-envelope");
  // Entry with status != present must not validate.
  expectReject(
      "{\"schemaVersion\": 1, \"tokenSnapshot\": \"d\", \"components\": "
      "[{\"name\": \"N\", \"primitive\": \"P\", \"tokens\": [\"t\"], "
      "\"states\": [\"enabled\", \"hovered\", \"focused\", \"pressed\", "
      "\"dragged\", \"disabled\", \"error\"], \"adaptive\": true, "
      "\"semantics\": \"S\", \"goldenTest\": \"G\", "
      "\"status\": \"missing\"}]}",
      "status-missing");
  // Huge input: rejected at the size gate without a giant scan, and the
  // scanner itself rejects blank-huge content without hanging.
  {
    const std::string huge(kMaxDocBytes + 1, ' ');
    if (docSizeOk(huge)) {
      std::printf("FAIL adversarial pin accepted: huge-size-gate\n");
      ok = false;
    } else {
      std::printf("PASS adversarial pin rejected: huge-size-gate\n");
    }
    expectReject(huge, "huge-blank-content");
  }
  // Deep nesting (512 nested arrays as the components value): rejected
  // by the kMaxNestDepth bound, quickly.
  {
    std::string deep =
        "{\"schemaVersion\": 1, \"tokenSnapshot\": \"d\", "
        "\"components\": ";
    deep += std::string(512, '[');
    deep += std::string(512, ']');
    deep += "}";
    expectReject(deep, "deep-nesting");
  }
  // argv handling: no-arg, too-many-arg, and missing file all fail
  // closed with exit 2 (usage/IO), never 0 and never a crash.
  {
    char prog[] = "m3_matrix_check";
    char* noArg[] = {prog};
    if (runValidator(1, noArg) != 2) {
      std::printf("FAIL adversarial pin accepted: no-arg-argv\n");
      ok = false;
    } else {
      std::printf("PASS adversarial pin rejected: no-arg-argv\n");
    }
    char extra[] = "x";
    char* tooMany[] = {prog, extra, extra};
    if (runValidator(3, tooMany) != 2) {
      std::printf("FAIL adversarial pin accepted: too-many-arg-argv\n");
      ok = false;
    } else {
      std::printf("PASS adversarial pin rejected: too-many-arg-argv\n");
    }
    char missing[] = "/nonexistent-dir-0x5f/m3_matrix.json";
    char* missingArg[] = {prog, missing};
    if (runValidator(2, missingArg) != 2) {
      std::printf("FAIL adversarial pin accepted: missing-file\n");
      ok = false;
    } else {
      std::printf("PASS adversarial pin rejected: missing-file\n");
    }
  }
  // Minimal valid document still accepted: the size/depth bounds must
  // not over-reject legitimate input.
  {
    const std::string minimal =
        "{\"schemaVersion\": 1, \"tokenSnapshot\": \"2026-10-28\", "
        "\"components\": [{\"name\": \"N\", \"primitive\": \"P\", "
        "\"tokens\": [\"t\"], \"states\": [\"enabled\", \"hovered\", "
        "\"focused\", \"pressed\", \"dragged\", \"disabled\", "
        "\"error\"], \"adaptive\": true, \"semantics\": \"S\", "
        "\"goldenTest\": \"G\", \"status\": \"present\"}]}";
    g_verbose = false;
    const int rc = validateDocument(minimal);
    g_verbose = true;
    if (rc != 0 || !docSizeOk(minimal)) {
      std::printf("FAIL adversarial pin rejected valid minimal doc\n");
      ok = false;
    } else {
      std::printf("PASS adversarial pin accepts valid minimal doc\n");
    }
  }
  return ok;
}

int runValidator(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: m3_matrix_check <path/to/m3_matrix.json>\n");
    return 2;
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::printf("FAIL open %s\n", argv[1]);
    return 2;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string doc = ss.str();
  if (!docSizeOk(doc)) {
    std::printf("FAIL too large %u bytes (limit %u)\n",
                (unsigned)doc.size(), (unsigned)kMaxDocBytes);
    return 1;
  }
  g_verbose = true;
  const int rc = validateDocument(doc);
  if (rc != 0) {
    std::printf("m3_matrix_check: %d failing\n", rc);
    return 1;
  }
  std::printf("m3_matrix_check: all passing\n");
  if (!runAdversarialPins()) {
    std::printf("m3_matrix_check: adversarial pins failing\n");
    return 1;
  }
  std::printf("m3_matrix_check: adversarial pins all passing\n");
  return 0;
}

int main(int argc, char** argv) { return runValidator(argc, argv); }
