#include "motion/m3_tokens.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace motion {
namespace {

void setError(std::string* errorOut, const std::string& message) {
  if (errorOut != nullptr) *errorOut = message;
}

// Finds the position of a quoted JSON key `"key"`, or npos.
std::string::size_type findKey(const std::string& text,
                               const std::string& key) {
  return text.find("\"" + key + "\"");
}

bool parseQuotedStringAfter(const std::string& text,
                            std::string::size_type colonPos,
                            std::string* valueOut) {
  const std::string::size_type open = text.find('"', colonPos + 1);
  if (open == std::string::npos) return false;
  const std::string::size_type close = text.find('"', open + 1);
  if (close == std::string::npos) return false;
  *valueOut = text.substr(open + 1, close - open - 1);
  return true;
}

bool parseDoubleAfter(const std::string& text, std::string::size_type colonPos,
                      double* valueOut) {
  const std::string::size_type start = text.find_first_of("-+0123456789.",
                                                          colonPos + 1);
  if (start == std::string::npos) return false;
  const char* begin = text.c_str() + start;
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) return false;
  *valueOut = value;
  return true;
}

// Finds the `{...}` object span for `"key": {...}` searching from
// `fromPos`. On success sets [openOut, closeOut] to the brace range and
// returns true. Keeps the parser dependency-free while preventing a key
// string appearing inside a *value* (or a sibling dimension) from matching.
bool findObjectSpan(const std::string& text, const std::string& key,
                    std::string::size_type fromPos,
                    std::string::size_type* openOut,
                    std::string::size_type* closeOut) {
  const std::string::size_type keyPos =
      text.find("\"" + key + "\"", fromPos);
  if (keyPos == std::string::npos) return false;
  const std::string::size_type colon = text.find(':', keyPos);
  if (colon == std::string::npos) return false;
  const std::string::size_type open = text.find('{', colon);
  if (open == std::string::npos) return false;
  int depth = 0;
  for (std::string::size_type i = open; i < text.size(); ++i) {
    if (text[i] == '{') ++depth;
    if (text[i] == '}') {
      --depth;
      if (depth == 0) {
        *openOut = open;
        *closeOut = i;
        return true;
      }
    }
  }
  return false;
}

// Parses `"key": <number>` searching only within [spanOpen, spanClose].
bool parseNumberKeyIn(const std::string& text, const std::string& key,
                      std::string::size_type spanOpen,
                      std::string::size_type spanClose, float* valueOut) {
  const std::string::size_type keyPos =
      text.find("\"" + key + "\"", spanOpen);
  if (keyPos == std::string::npos || keyPos > spanClose) return false;
  const std::string::size_type colon = text.find(':', keyPos);
  if (colon == std::string::npos || colon > spanClose) return false;
  double value = 0.0;
  if (!parseDoubleAfter(text, colon, &value)) return false;
  *valueOut = static_cast<float>(value);
  return true;
}

// Parses `"key": "<string>"` searching only within [spanOpen, spanClose].
bool parseStringKeyIn(const std::string& text, const std::string& key,
                      std::string::size_type spanOpen,
                      std::string::size_type spanClose,
                      std::string* valueOut) {
  const std::string::size_type keyPos =
      text.find("\"" + key + "\"", spanOpen);
  if (keyPos == std::string::npos || keyPos > spanClose) return false;
  const std::string::size_type colon = text.find(':', keyPos);
  if (colon == std::string::npos || colon > spanClose) return false;
  return parseQuotedStringAfter(text, colon, valueOut);
}

// Parses `"key": {"stiffness": N, "dampingRatio": M}` searching only within
// [spanOpen, spanClose].
bool parseSpringKeyIn(const std::string& text, const std::string& key,
                      std::string::size_type spanOpen,
                      std::string::size_type spanClose,
                      SpringSpec* specOut) {
  const std::string::size_type keyPos =
      text.find("\"" + key + "\"", spanOpen);
  if (keyPos == std::string::npos || keyPos > spanClose) return false;
  const std::string::size_type colon = text.find(':', keyPos);
  if (colon == std::string::npos || colon > spanClose) return false;
  const std::string::size_type open = text.find('{', colon);
  if (open == std::string::npos || open > spanClose) return false;
  float stiffness = 0.0f;
  float dampingRatio = 0.0f;
  if (!parseNumberKeyIn(text, "stiffness", open, spanClose, &stiffness))
    return false;
  if (!parseNumberKeyIn(text, "dampingRatio", open, spanClose, &dampingRatio))
    return false;
  specOut->stiffness = stiffness;
  specOut->dampingRatio = dampingRatio;
  return true;
}

}  // namespace

M3TokenSet motion_loadM3Tokens(const std::string& jsonPath,
                               std::string* errorOut) {
  // Missing file: empty set, error names the file input.
  std::ifstream in(jsonPath, std::ios::binary);
  if (!in) {
    setError(errorOut, "m3_tokens: missing file: " + jsonPath);
    return M3TokenSet{};
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();

  // Each top-level dimension must be present; otherwise the empty set with
  // the field named.
  const char* requiredFields[] = {"version", "durationsMs", "easings",
                                  "spatialSprings", "effectsSprings"};
  for (const char* field : requiredFields) {
    if (findKey(text, field) == std::string::npos) {
      setError(errorOut,
               std::string("m3_tokens: missing field: ") + field);
      return M3TokenSet{};
    }
  }

  // Version: non-empty string.
  const std::string::size_type versionKey = findKey(text, "version");
  const std::string::size_type versionColon = text.find(':', versionKey);
  std::string version;
  if (versionColon == std::string::npos ||
      !parseQuotedStringAfter(text, versionColon, &version) ||
      version.empty()) {
    setError(errorOut, "m3_tokens: missing field: version");
    return M3TokenSet{};
  }

  M3TokenSet set;
  set.version = version;

  static const char* kDurations[] = {
      "short1", "short2", "short3", "short4",  "medium1", "medium2",
      "medium3", "medium4", "long1", "long2", "long3", "long4"};
  static const char* kEasings[] = {"standard", "emphasized", "accelerate",
                                   "decelerate"};
  static const char* kSpatial[] = {"position", "size", "shape"};
  static const char* kEffects[] = {"colour", "opacity"};

  // Each dimension is parsed within its own object span so a key string
  // appearing inside a value (e.g. an easing string) or a sibling object
  // can never satisfy a lookup.
  std::string::size_type dimOpen = 0, dimClose = 0;
  if (!findObjectSpan(text, "durationsMs", 0, &dimOpen, &dimClose)) {
    setError(errorOut, "m3_tokens: missing field: durationsMs");
    return M3TokenSet{};
  }
  // Durations: all members required; each must parse to a positive value.
  for (const char* key : kDurations) {
    float value = 0.0f;
    if (!parseNumberKeyIn(text, key, dimOpen, dimClose, &value) ||
        !(value > 0.0f)) {
      setError(errorOut,
               std::string("m3_tokens: missing field: durationsMs.") + key);
      return M3TokenSet{};
    }
    set.durationsMs[key] = value;
  }

  if (!findObjectSpan(text, "easings", 0, &dimOpen, &dimClose)) {
    setError(errorOut, "m3_tokens: missing field: easings");
    return M3TokenSet{};
  }
  // Easings: all members required; each must be a non-empty string.
  for (const char* key : kEasings) {
    std::string value;
    if (!parseStringKeyIn(text, key, dimOpen, dimClose, &value) ||
        value.empty()) {
      setError(errorOut,
               std::string("m3_tokens: missing field: easings.") + key);
      return M3TokenSet{};
    }
    set.easings[key] = value;
  }

  if (!findObjectSpan(text, "spatialSprings", 0, &dimOpen, &dimClose)) {
    setError(errorOut, "m3_tokens: missing field: spatialSprings");
    return M3TokenSet{};
  }
  // Springs: all members required with positive stiffness/dampingRatio.
  for (const char* key : kSpatial) {
    SpringSpec spec;
    if (!parseSpringKeyIn(text, key, dimOpen, dimClose, &spec) ||
        !(spec.stiffness > 0.0f) || !(spec.dampingRatio > 0.0f)) {
      setError(errorOut,
               std::string("m3_tokens: missing field: spatialSprings.") +
                   key);
      return M3TokenSet{};
    }
    set.spatialSprings[key] = spec;
  }
  if (!findObjectSpan(text, "effectsSprings", 0, &dimOpen, &dimClose)) {
    setError(errorOut, "m3_tokens: missing field: effectsSprings");
    return M3TokenSet{};
  }
  for (const char* key : kEffects) {
    SpringSpec spec;
    if (!parseSpringKeyIn(text, key, dimOpen, dimClose, &spec) ||
        !(spec.stiffness > 0.0f) || !(spec.dampingRatio > 0.0f)) {
      setError(errorOut,
               std::string("m3_tokens: missing field: effectsSprings.") +
                   key);
      return M3TokenSet{};
    }
    set.effectsSprings[key] = spec;
  }

  setError(errorOut, "");
  return set;
}

}  // namespace motion
