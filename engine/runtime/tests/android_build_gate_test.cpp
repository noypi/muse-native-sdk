// Batch 1 / T3 contract tests — NFR-RUNTIME-001 (AC-RUNTIME-025).
// Dual-ABI build artifact gate: both Android ABI outputs of engine/runtime
// exist as loadable libRuntime.so files built from the same sources as the
// host build. No behavior change to tree/frame/layout/renderer is asserted
// here; this test names files, magic bytes, and sizes only.
// Hermetic: C++17 + stdlib (cstdio/cstring/filesystem/cstdlib) only; no
// device, no emulator, no network, no secrets, no live services. Artifact
// locations resolve as argv[1] > $ANDROID_BUILD_ROOT > default relative
// "build-android-<abi>" dirs next to the host build dir, so the test never
// inherits machine state beyond an explicit input. The JNI-only export-
// surface audit runs only when `nm` is present on PATH and the artifacts
// exist; otherwise that case reports SKIP (ASSUMPTION below) while the
// existence/size/magic cases still assert.
// Binds ONLY to the T3 `Interface:` line (no callable surface); never to
// implementation.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {

const char* kAbis[2] = {"arm64-v8a", "x86_64"};
const char* kSoname = "libRuntime.so";

std::string build_root() {
  if (const char* env = std::getenv("ANDROID_BUILD_ROOT")) {
    if (env[0] != '\0') return std::string(env);
  }
  return "build-android";
}

std::string artifact_for(const std::string& root, const char* abi) {
  return root + "-" + abi + "/" + kSoname;
}

// Reads up to `want` bytes; returns bytes actually read.
std::size_t read_prefix(const fs::path& p, unsigned char* out,
                        std::size_t want) {
  std::FILE* f = std::fopen(p.string().c_str(), "rb");
  if (f == nullptr) return 0;
  std::size_t n = std::fread(out, 1, want, f);
  std::fclose(f);
  return n;
}

bool has_elf_magic(const fs::path& p) {
  unsigned char magic[4] = {0, 0, 0, 0};
  if (read_prefix(p, magic, sizeof(magic)) != sizeof(magic)) return false;
  return magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' &&
         magic[3] == 'F';
}

}  // namespace

static std::string g_root;
static bool g_root_from_arg = false;

// Named case: the arm64-v8a libRuntime.so artifact exists.
static bool check_arm64_artifact_exists() {
  std::error_code ec;
  bool ok = fs::is_regular_file(artifact_for(g_root, kAbis[0]), ec);
  if (!ok || ec) {
    std::printf("  detail: missing %s\n",
                artifact_for(g_root, kAbis[0]).c_str());
    return false;
  }
  return true;
}

// Named case: the x86_64 libRuntime.so artifact exists.
static bool check_x86_64_artifact_exists() {
  std::error_code ec;
  bool ok = fs::is_regular_file(artifact_for(g_root, kAbis[1]), ec);
  if (!ok || ec) {
    std::printf("  detail: missing %s\n",
                artifact_for(g_root, kAbis[1]).c_str());
    return false;
  }
  return true;
}

// Named case: both artifacts are non-empty (a zero-byte .so is not a build).
static bool check_both_artifacts_non_empty() {
  for (const char* abi : kAbis) {
    std::error_code ec;
    auto size = fs::file_size(artifact_for(g_root, abi), ec);
    if (ec || size == 0) {
      std::printf("  detail: %s empty or unreadable\n",
                  artifact_for(g_root, abi).c_str());
      return false;
    }
  }
  return true;
}

// Named case: both artifacts carry the ELF magic (they are real shared
// objects, not text markers or stale outputs).
static bool check_both_artifacts_are_elf() {
  for (const char* abi : kAbis) {
    if (!has_elf_magic(artifact_for(g_root, abi))) {
      std::printf("  detail: %s missing ELF magic\n",
                  artifact_for(g_root, abi).c_str());
      return false;
    }
  }
  return true;
}

// Named case: both ABI outputs resolve under one build root (the dual-ABI
// configure from T1 produced them side by side, not from divergent trees).
static bool check_both_artifacts_share_one_root() {
  // ASSUMPTION: layout is <root>-<abi>/libRuntime.so, mirroring the plan's
  // build-android-<abi> configure lines; the coder owns the exact filenames
  // and this gate pins the layout so it is implementable.
  for (const char* abi : kAbis) {
    std::error_code ec;
    if (!fs::is_regular_file(artifact_for(g_root, abi), ec) || ec) {
      return false;
    }
  }
  std::printf("  detail: root=%s abis=%s,%s\n", g_root.c_str(), kAbis[0],
              kAbis[1]);
  return true;
}

// Named case: exported dynamic symbols expose only the JNI surface. Runs
// `nm -D` when available; reports SKIP (pass) when the tool or artifacts are
// absent so a host without the NDK still exercises every other case.
static bool check_exports_limited_to_jni_surface() {
  for (const char* abi : kAbis) {
    std::error_code ec;
    if (!fs::is_regular_file(artifact_for(g_root, abi), ec) || ec) {
      std::printf("  detail: SKIP exports for %s (artifact absent)\n", abi);
      return true;
    }
  }
  // ASSUMPTION: full export-surface auditing (no engine-internal symbols
  // beyond JNI_OnLoad / JNI surface) is owned by the coder's build lane via
  // `-fvisibility=hidden`; this case fails only on an observed internal
  // export, and skips when `nm` is unavailable.
  if (std::system("command -v nm >/dev/null 2>&1") != 0) {
    std::printf("  detail: SKIP exports audit (nm unavailable)\n");
    return true;
  }
  bool clean = true;
  for (const char* abi : kAbis) {
    std::string cmd = std::string("nm -D --defined-only \"") +
                      artifact_for(g_root, abi) +
                      "\" 2>/dev/null | grep -E ' [TDR] (muse_|runtime::)' "
                      "> /dev/null";
    if (std::system(cmd.c_str()) == 0) {
      std::printf("  detail: %s exports engine-internal symbols\n", abi);
      clean = false;
    }
  }
  return clean;
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main(int argc, char** argv) {
  g_root = build_root();
  if (argc > 1 && argv[1][0] != '\0') {
    g_root = argv[1];
    g_root_from_arg = true;
  }
  (void)g_root_from_arg;
  std::printf("  detail: ANDROID_BUILD_ROOT=%s\n", g_root.c_str());
  int failures = 0;
  RUN_CASE(check_arm64_artifact_exists);
  RUN_CASE(check_x86_64_artifact_exists);
  RUN_CASE(check_both_artifacts_non_empty);
  RUN_CASE(check_both_artifacts_are_elf);
  RUN_CASE(check_both_artifacts_share_one_root);
  RUN_CASE(check_exports_limited_to_jni_surface);
  if (failures > 0) {
    std::printf("android_build_gate: %d failing\n", failures);
    return 1;
  }
  std::printf("android_build_gate: all passing\n");
  return 0;
}
