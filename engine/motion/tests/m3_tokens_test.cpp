// T1 contract tests — FR-MOTION-013 / AC-MOTION-042, AC-MOTION-044.
// Hermetic: C++17 + committed motion headers only; no network, no env/
// config reads, no live services. Deterministic: fixture JSON is authored
// inline and written to a unique temp path per case; no wall clock, no
// randomness. Fixture files are created by the test itself and removed in
// teardown; the loader under test receives the path as input.
// Binds to the T1 `Interface:` line (reproduced verbatim in unit-tests/T1.md).
//
// ASSUMPTION (token key names): the plan names the four dimensions and their
// members (durations short1-long4; easings standard/emphasized/accelerate/
// decelerate; spatial springs position/size/shape; effects springs
// colour/opacity) but no literal key spellings. The fixture below uses the
// lowercase key spellings shown; if the coder pins different spellings, the
// fixture keys update via reconciliation (expectations unchanged).
// ASSUMPTION (values): exact duration ms values and easing string encodings
// are NOT pinned by the plan/spec — tests assert presence/shape (durations
// present and > 0, easings present and non-empty, springs parse into
// SpringSpec with stiffness > 0 and dampingRatio > 0), never specific
// numbers. ASSUMPTION (constant names): the plan requires token-name
// constants in the header without naming them; the `kM3*` spellings below
// are the test's proposal and reconcile by rename if the coder differs.
// ASSUMPTION (error channel): `errorOut` may be nullptr (loader still
// returns the empty set); when non-null, failures name the field.
#include <cstdio>
#include <fstream>
#include <string>

#include "motion/m3_tokens.h"

namespace {

const char* kPinnedVersion = "1.0.0-pinned";

// Representative m3.json doc. Values are placeholders (see ASSUMPTION above);
// shape (four dimensions + version) is what the contract pins.
std::string makeFixtureJson(bool withVersion) {
  std::string json = "{\n";
  if (withVersion) {
    json += "  \"version\": \"" + std::string(kPinnedVersion) + "\",\n";
  }
  json += "  \"durationsMs\": {\n";
  json += "    \"short1\": 50, \"short2\": 100, \"short3\": 150, \"short4\": 200,\n";
  json += "    \"medium1\": 250, \"medium2\": 300, \"medium3\": 350, \"medium4\": 400,\n";
  json += "    \"long1\": 450, \"long2\": 500, \"long3\": 550, \"long4\": 600\n";
  json += "  },\n";
  json += "  \"easings\": {\n";
  json += "    \"standard\": \"cubic-bezier(0.2, 0.0, 0.0, 1.0)\",\n";
  json += "    \"emphasized\": \"cubic-bezier(0.05, 0.7, 0.1, 1.0)\",\n";
  json += "    \"accelerate\": \"cubic-bezier(0.3, 0.0, 0.8, 0.15)\",\n";
  json += "    \"decelerate\": \"cubic-bezier(0.05, 0.7, 0.1, 1.0)\"\n";
  json += "  },\n";
  json += "  \"spatialSprings\": {\n";
  json += "    \"position\": {\"stiffness\": 180.0, \"dampingRatio\": 1.0},\n";
  json += "    \"size\": {\"stiffness\": 160.0, \"dampingRatio\": 1.0},\n";
  json += "    \"shape\": {\"stiffness\": 140.0, \"dampingRatio\": 1.0}\n";
  json += "  },\n";
  json += "  \"effectsSprings\": {\n";
  json += "    \"colour\": {\"stiffness\": 120.0, \"dampingRatio\": 1.0},\n";
  json += "    \"opacity\": {\"stiffness\": 100.0, \"dampingRatio\": 1.0}\n";
  json += "  }\n";
  json += "}\n";
  return json;
}

int gFixtureSeq = 0;

std::string writeFixture(const std::string& body) {
  std::string path =
      "/tmp/m3_tokens_test_" + std::to_string(gFixtureSeq++) + ".json";
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << body;
  out.close();
  return path;
}

void removeFixture(const std::string& path) { std::remove(path.c_str()); }

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

// Loads the doc; all four dimensions are present with the expected members.
bool check_loads_all_four_dimensions() {
  std::string path = writeFixture(makeFixtureJson(true));
  std::string error;
  motion::M3TokenSet set = motion::motion_loadM3Tokens(path, &error);
  removeFixture(path);
  if (!error.empty()) return false;
  const char* durations[] = {"short1", "short2", "short3", "short4",
                             "medium1", "medium2", "medium3", "medium4",
                             "long1", "long2", "long3", "long4"};
  for (const char* key : durations) {
    auto it = set.durationsMs.find(key);
    if (it == set.durationsMs.end()) return false;
    if (!(it->second > 0.0f)) return false;  // shape only, not literals
  }
  const char* easings[] = {"standard", "emphasized", "accelerate",
                           "decelerate"};
  for (const char* key : easings) {
    auto it = set.easings.find(key);
    if (it == set.easings.end()) return false;
    if (it->second.empty()) return false;  // encoding not pinned
  }
  const char* spatial[] = {"position", "size", "shape"};
  for (const char* key : spatial) {
    auto it = set.spatialSprings.find(key);
    if (it == set.spatialSprings.end()) return false;
    if (!(it->second.stiffness > 0.0f)) return false;
    if (!(it->second.dampingRatio > 0.0f)) return false;
  }
  const char* effects[] = {"colour", "opacity"};
  for (const char* key : effects) {
    auto it = set.effectsSprings.find(key);
    if (it == set.effectsSprings.end()) return false;
    if (!(it->second.stiffness > 0.0f)) return false;
    if (!(it->second.dampingRatio > 0.0f)) return false;
  }
  return true;
}

// Version field is non-empty and round-trips across loads.
bool check_version_nonempty_and_roundtrips() {
  std::string path = writeFixture(makeFixtureJson(true));
  std::string error;
  motion::M3TokenSet first = motion::motion_loadM3Tokens(path, &error);
  motion::M3TokenSet second = motion::motion_loadM3Tokens(path, &error);
  removeFixture(path);
  if (!error.empty()) return false;
  if (first.version.empty()) return false;
  return first.version == second.version &&
         first.version == std::string(kPinnedVersion);
}

// Missing version: loader returns empty set with error naming `version`.
bool check_missing_version_error_names_version() {
  std::string path = writeFixture(makeFixtureJson(false));
  std::string error;
  motion::M3TokenSet set = motion::motion_loadM3Tokens(path, &error);
  removeFixture(path);
  if (error.empty()) return false;
  if (!contains(error, "version")) return false;
  return set.version.empty();
}

// Missing file: loader returns empty set with a non-empty error.
bool check_missing_file_reports_error() {
  std::string error;
  motion::M3TokenSet set = motion::motion_loadM3Tokens(
      "/tmp/m3_tokens_test_does_not_exist_000000.json", &error);
  if (error.empty()) return false;
  return set.version.empty() && set.durationsMs.empty();
}

// Null errorOut is tolerated: load still succeeds without crashing.
bool check_null_error_out_tolerated() {
  std::string path = writeFixture(makeFixtureJson(true));
  motion::M3TokenSet set = motion::motion_loadM3Tokens(path, nullptr);
  removeFixture(path);
  return !set.version.empty();
}

// Component-references-name check: token-name constants exist in the header
// (ASSUMPTION: `kM3*` spellings; reconcile-by-rename if the coder differs).
// Components reference these names, never literals.
bool check_token_name_constants_exist() {
  const std::string dShort1 = motion::kM3DurationShort1;
  const std::string dLong4 = motion::kM3DurationLong4;
  const std::string eStandard = motion::kM3EasingStandard;
  const std::string eEmphasized = motion::kM3EasingEmphasized;
  const std::string sPosition = motion::kM3SpatialPosition;
  const std::string sSize = motion::kM3SpatialSize;
  const std::string sShape = motion::kM3SpatialShape;
  const std::string fColour = motion::kM3EffectsColour;
  const std::string fOpacity = motion::kM3EffectsOpacity;
  if (dShort1.empty() || dLong4.empty()) return false;
  if (eStandard.empty() || eEmphasized.empty()) return false;
  if (sPosition.empty() || sSize.empty() || sShape.empty()) return false;
  if (fColour.empty() || fOpacity.empty()) return false;
  // Constants resolve through the loaded maps (names, not literals).
  std::string path = writeFixture(makeFixtureJson(true));
  std::string error;
  motion::M3TokenSet set = motion::motion_loadM3Tokens(path, &error);
  removeFixture(path);
  if (!error.empty()) return false;
  return set.durationsMs.count(dShort1) > 0 &&
         set.durationsMs.count(dLong4) > 0 &&
         set.easings.count(eStandard) > 0 &&
         set.spatialSprings.count(sPosition) > 0 &&
         set.effectsSprings.count(fColour) > 0 &&
         set.effectsSprings.count(fOpacity) > 0;
}

}  // namespace

#define RUN_CASE(name)                     \
  do {                                     \
    if (!(name)()) {                       \
      std::printf("FAIL %s\n", #name);     \
      ++failures;                          \
    } else {                               \
      std::printf("PASS %s\n", #name);     \
    }                                      \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_loads_all_four_dimensions);
  RUN_CASE(check_version_nonempty_and_roundtrips);
  RUN_CASE(check_missing_version_error_names_version);
  RUN_CASE(check_missing_file_reports_error);
  RUN_CASE(check_null_error_out_tolerated);
  RUN_CASE(check_token_name_constants_exist);
  if (failures > 0) {
    std::printf("m3_tokens_test: %d failing\n", failures);
    return 1;
  }
  std::printf("m3_tokens_test: all passing\n");
  return 0;
}
