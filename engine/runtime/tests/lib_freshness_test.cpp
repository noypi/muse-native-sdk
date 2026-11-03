// Batch 1 RED pin — T3 (demo-fallback-boot, GR-08 T1.1).
//
// APK-inner libRuntime must match the fresh MUSE_NATIVE_HOME build by hash
// at pack time. Hermetic analogue: this TU asserts the demo packaging recipe
// (`examples/muse-demo/Makefile`, `apk` target) gates on hash equality
// between each staged `lib/<abi>/libRuntime.so` and its fresh
// `$(MUSE_NATIVE_HOME)/<abi>/libRuntime.so` source, failing the pack with a
// `STALE_LIB` diagnostic on mismatch. Binds ONLY to the plan Interface
// `assertLibFresh(apkSoHash, nativeHomeSoHash): void` (recipe-text form) and
// the committed Makefile text — no env, no device, no network, no
// MUSE_NATIVE_HOME.
//
// Spec refs: FR-SAMPLE-009, AC-SAMPLE-023 (fresh engine bits at pack).
//
// Expected RED (pre-T5): the recipe plain-`cp`s the four ABI libs with no
// hash comparison and no STALE_LIB marker, so the gate cases below FAIL
// until the coder ports the m3-gallery hash guards to the muse-demo recipe.
//
// Path anchor: `__FILE__`-relative (tests/ -> worktree root is 3 levels
// up), same convention as flappy_real_tree_acceptance_test.cpp. Ctest runs
// from the build dir, so the anchor — never cwd, never env — locates the
// committed recipe.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#define RUN_CASE(name)                  \
  do {                                  \
    if (!(name)()) {                    \
      std::printf("FAIL %s\n", #name);  \
      ++failures;                       \
    } else {                            \
      std::printf("PASS %s\n", #name);  \
    }                                   \
  } while (0)

namespace {

std::string RecipePath() {
  std::string here = __FILE__;
  const std::string suffix =
      "engine/runtime/tests/lib_freshness_test.cpp";
  const auto pos = here.rfind(suffix);
  if (pos != std::string::npos) return here.substr(0, pos) +
                                        "examples/muse-demo/Makefile";
  return "examples/muse-demo/Makefile";
}

std::string ReadFile(const std::string& path, bool* ok) {
  std::ifstream f(path.c_str());
  if (!f.good()) {
    *ok = false;
    return "";
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  *ok = true;
  return ss.str();
}

std::string RecipeText() {
  static std::string text;
  static bool loaded = false;
  if (!loaded) {
    bool ok = false;
    text = ReadFile(RecipePath(), &ok);
    if (!ok) text = "";
    loaded = true;
  }
  return text;
}

// Interface under test (recipe-text form of
// `assertLibFresh(apkSoHash, nativeHomeSoHash): void`): the recipe must
// compare each staged libRuntime.so against the fresh MUSE_NATIVE_HOME
// build by hash (cmp -s / sha256sum --check / equivalent) before packing.
// Throws STALE_LIB-shaped failure (returns false here) when the gate is
// absent.
bool RecipeGatesLibHash(const std::string& recipe) {
  const bool mentionsStagedLib =
      recipe.find("STAGING_DIR)/lib") != std::string::npos;
  const bool mentionsFreshLib =
      recipe.find("MUSE_NATIVE_HOME") != std::string::npos &&
      recipe.find("libRuntime.so") != std::string::npos;
  const bool comparesByHash =
      recipe.find("sha256sum") != std::string::npos ||
      recipe.find("cmp -s") != std::string::npos ||
      recipe.find("cmp --silent") != std::string::npos;
  return mentionsStagedLib && mentionsFreshLib && comparesByHash;
}

bool check_recipe_found() {
  bool ok = false;
  (void)ReadFile(RecipePath(), &ok);
  if (!ok) {
    std::printf("  detail: recipe unreadable: %s\n", RecipePath().c_str());
    return false;
  }
  return true;
}

bool check_recipe_gates_lib_hash() {
  const std::string recipe = RecipeText();
  if (recipe.empty()) {
    std::printf("  detail: recipe empty/unreadable\n");
    return false;
  }
  if (!RecipeGatesLibHash(recipe)) {
    std::printf("  detail: STALE_LIB risk: recipe copies libRuntime.so with "
                "no staged-vs-fresh hash gate\n");
    return false;
  }
  return true;
}

bool check_recipe_reports_stale_lib() {
  const std::string recipe = RecipeText();
  if (recipe.empty()) return false;
  if (recipe.find("STALE_LIB") == std::string::npos) {
    std::printf("  detail: no STALE_LIB diagnostic in recipe\n");
    return false;
  }
  return true;
}

}  // namespace

int main() {
  int failures = 0;
  RUN_CASE(check_recipe_found);
  RUN_CASE(check_recipe_gates_lib_hash);
  RUN_CASE(check_recipe_reports_stale_lib);
  if (failures > 0) {
    std::printf("lib_freshness_test: %d failing\n", failures);
    return 1;
  }
  std::printf("lib_freshness_test: all passing\n");
  return 0;
}
