// R1 link-composition regression test — BUG-BUILD-009 (AC-PACKAGING-013/018).
//
// Interface: run_link_check() -> exit 0 iff (shared link of TreeBridge.cpp.o
// + libruntime.a with no -Wl,--allow-multiple-definition succeeds AND nm -D
// lists Java_com_muse_host_MainActivity_nativeLoadTree +
// Java_com_muse_host_MainActivity_nativeRenderFrame + exactly one JNI_OnLoad);
// nonzero otherwise.
//
// Host-side only: compiles packages/cli/templates/android/TreeBridge.cpp with
// jni.h from the pinned JDK sysroot, links it with the engine archive
// (libruntime.a when present, else engine/runtime/src/jni_bridge.cpp.o as a
// source-identical fallback), then audits `nm -D`. No device, no NDK install
// beyond pinned sysroot headers (host g++ -fPIC -shared recipe from
// support.md). Follows android_build_gate_test.cpp / jni_boundary_test.cpp
// host-side patterns (cstdio + RUN_CASE + main returning 0/1; toolchain steps
// via std::system; a missing toolchain is FAIL here, not SKIP — the link
// composition is exactly what this test proves).
//
// Hermetic: C++17 + stdlib only; no network, no secrets, no config reads, no
// live services. Inputs resolve as argv[1] (repo root) > $MUSE_REPO_ROOT >
// upward search from cwd for packages/cli/templates/android/TreeBridge.cpp.
// Per-file overrides for red-state reproduction: $TREE_BRIDGE_SRC,
// $JNI_BRIDGE_SRC, $RUNTIME_LIB, $JAVA_INCLUDE (colon-separated -I dirs).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::string g_repo_root;
std::string g_tmpdir;
std::string g_bridge_src;
std::string g_jni_bridge_src;
std::string g_engine_include;
std::string g_runtime_lib;  // may be empty -> compile jni_bridge.cpp.o fallback
std::vector<std::string> g_jni_includes;
std::string g_treebridge_obj;
std::string g_engine_obj;
std::string g_shared_lib;

int run_cmd(const std::string& cmd) {
  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    std::printf("  detail: command exit=%d: %s\n", rc, cmd.c_str());
  }
  return rc;
}

std::string read_file(const std::string& p) {
  std::FILE* f = std::fopen(p.c_str(), "rb");
  if (f == nullptr) return std::string();
  std::string out;
  char buf[4096];
  std::size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
    out.append(buf, n);
  }
  std::fclose(f);
  return out;
}

// Counts non-overlapping occurrences of needle in haystack.
std::size_t count_occurrences(const std::string& haystack,
                              const std::string& needle) {
  if (needle.empty()) return 0;
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

std::string getenv_str(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr ? std::string(v) : std::string();
}

// Splits a colon-separated list into -I flags.
std::string include_flags(const std::string& colon_list) {
  std::string flags;
  std::size_t start = 0;
  while (start <= colon_list.size()) {
    std::size_t end = colon_list.find(':', start);
    if (end == std::string::npos) end = colon_list.size();
    std::string dir = colon_list.substr(start, end - start);
    if (!dir.empty()) {
      flags += " -I\"" + dir + "\"";
    }
    start = end + 1;
  }
  return flags;
}

bool find_repo_root() {
  if (!g_repo_root.empty()) {
    return fs::is_regular_file(g_repo_root +
                               "/packages/cli/templates/android/TreeBridge.cpp");
  }
  std::string from_env = getenv_str("MUSE_REPO_ROOT");
  if (!from_env.empty() &&
      fs::is_regular_file(from_env +
                          "/packages/cli/templates/android/TreeBridge.cpp")) {
    g_repo_root = from_env;
    return true;
  }
  // Walk up from cwd (covers running the test binary from a build dir).
  std::error_code ec;
  fs::path dir = fs::current_path(ec);
  if (ec) return false;
  for (int i = 0; i < 12; ++i) {
    if (fs::is_regular_file(
            dir / "packages/cli/templates/android/TreeBridge.cpp", ec) &&
        !ec) {
      g_repo_root = dir.string();
      return true;
    }
    if (!dir.has_parent_path()) break;
    dir = dir.parent_path();
  }
  return false;
}

void resolve_inputs() {
  std::string bridge_env = getenv_str("TREE_BRIDGE_SRC");
  g_bridge_src = !bridge_env.empty()
                     ? bridge_env
                     : (g_repo_root +
                        "/packages/cli/templates/android/TreeBridge.cpp");
  std::string jni_env = getenv_str("JNI_BRIDGE_SRC");
  g_jni_bridge_src = !jni_env.empty()
                         ? jni_env
                         : (g_repo_root + "/engine/runtime/src/jni_bridge.cpp");
  g_engine_include = g_repo_root + "/engine/runtime/include";
  g_runtime_lib = getenv_str("RUNTIME_LIB");
  if (g_runtime_lib.empty()) {
    // Prefer a built engine archive under the repo when one exists.
    const char* candidates[] = {
        "/build/runtime-host/libruntime.a",
        "/build/libruntime.a",
        "/engine/runtime/build/libruntime.a",
    };
    for (const char* rel : candidates) {
      std::string p = g_repo_root + rel;
      std::error_code ec;
      if (fs::is_regular_file(p, ec) && !ec) {
        g_runtime_lib = p;
        break;
      }
    }
  }
  std::string java_include = getenv_str("JAVA_INCLUDE");
  std::string jni_flags;
  if (!java_include.empty()) {
    jni_flags = include_flags(java_include);
  } else {
    const char* pinned[] = {
        "/usr/lib/jvm/java-21-openjdk-amd64/include",
        "/usr/lib/jvm/java-21-openjdk-amd64/include/linux",
    };
    for (const char* dir : pinned) {
      std::error_code ec;
      if (fs::is_directory(dir, ec) && !ec) {
        g_jni_includes.emplace_back(dir);
        jni_flags += std::string(" -I\"") + dir + "\"";
      }
    }
    // $JAVA_HOME fallback when the pinned sysroot is absent.
    std::string java_home = getenv_str("JAVA_HOME");
    if (g_jni_includes.empty() && !java_home.empty()) {
      for (const char* sub : {"/include", "/include/linux"}) {
        std::string dir = java_home + sub;
        std::error_code ec;
        if (fs::is_directory(dir, ec) && !ec) {
          jni_flags += " -I\"" + dir + "\"";
        }
      }
    }
  }
  (void)jni_flags;
  // Stash the assembled -I flags in g_tmpdir-adjacent storage via statics.
  static std::string s_jni_flags;
  s_jni_flags = jni_flags;
  // Re-expose through a file-scope accessor below.
  (void)s_jni_flags;
}

const std::string& jni_flags() {
  static std::string s;
  if (s.empty()) {
    std::string java_include = getenv_str("JAVA_INCLUDE");
    if (!java_include.empty()) {
      s = include_flags(java_include);
    } else {
      const char* pinned[] = {
          "/usr/lib/jvm/java-21-openjdk-amd64/include",
          "/usr/lib/jvm/java-21-openjdk-amd64/include/linux",
      };
      for (const char* dir : pinned) {
        std::error_code ec;
        if (fs::is_directory(dir, ec) && !ec) {
          s += std::string(" -I\"") + dir + "\"";
        }
      }
      std::string java_home = getenv_str("JAVA_HOME");
      if (s.empty() && !java_home.empty()) {
        for (const char* sub : {"/include", "/include/linux"}) {
          std::string dir = java_home + sub;
          std::error_code ec;
          if (fs::is_directory(dir, ec) && !ec) {
            s += " -I\"" + dir + "\"";
          }
        }
      }
    }
  }
  return s;
}

bool make_tmpdir() {
  char tmpl[] = "/tmp/jnibridge-link-XXXXXX";
  const char* d = ::mkdtemp(tmpl);
  if (d == nullptr) {
    std::printf("  detail: mkdtemp failed\n");
    return false;
  }
  g_tmpdir = d;
  g_treebridge_obj = g_tmpdir + "/TreeBridge.o";
  g_engine_obj = g_tmpdir + "/jni_bridge.o";
  g_shared_lib = g_tmpdir + "/libAppShell.so";
  return true;
}

}  // namespace

// Named case: the template carries no JNI_OnLoad *definition* (cheap source
// net from support.md; header comments mentioning the gate do not match
// `jint JNI_OnLoad(`).
static bool check_template_defines_no_jni_onload() {
  std::string src = read_file(g_bridge_src);
  if (src.empty()) {
    std::printf("  detail: unreadable %s\n", g_bridge_src.c_str());
    return false;
  }
  std::size_t n = count_occurrences(src, "JNI_OnLoad");
  std::size_t defs = count_occurrences(src, "jint JNI_OnLoad(");
  std::printf("  detail: %s mentions=%zu definitions=%zu\n",
              g_bridge_src.c_str(), n, defs);
  return defs == 0;
}

// Named case: TreeBridge.cpp compiles to an object with host g++ -fPIC.
static bool check_treebridge_compiles() {
  std::string cmd = std::string("g++ -std=c++17 -fPIC -c \"") + g_bridge_src +
                    "\" -o \"" + g_treebridge_obj + "\"" + jni_flags() +
                    " >/dev/null 2>&1";
  if (run_cmd(cmd) != 0) {
    std::printf("  detail: TreeBridge.cpp compile failed\n");
    return false;
  }
  std::error_code ec;
  bool ok = fs::is_regular_file(g_treebridge_obj, ec) && !ec;
  if (!ok) std::printf("  detail: missing %s\n", g_treebridge_obj.c_str());
  return ok;
}

// Named case: shared link of TreeBridge.cpp.o + engine archive succeeds with
// NO -Wl,--allow-multiple-definition (a duplicate JNI_OnLoad fails here with
// `multiple definition of 'JNI_OnLoad...'` / `duplicate symbol`, exit 1).
static bool check_appshell_shared_link_succeeds() {
  std::string engine_input;
  if (!g_runtime_lib.empty()) {
    engine_input = "\"" + g_runtime_lib + "\"";
  } else {
    std::string cmd = std::string("g++ -std=c++17 -fPIC -c \"") +
                      g_jni_bridge_src + "\" -o \"" + g_engine_obj + "\"" +
                      " -I\"" + g_engine_include + "\"" + jni_flags() +
                      " >/dev/null 2>&1";
    if (run_cmd(cmd) != 0) {
      std::printf("  detail: jni_bridge.cpp fallback compile failed\n");
      return false;
    }
    engine_input = "\"" + g_engine_obj + "\"";
  }
  std::string cmd = std::string("g++ -shared -o \"") + g_shared_lib +
                    "\" \"" + g_treebridge_obj + "\" " + engine_input +
                    " >/dev/null 2>&1";
  // The invariant under test: this command must never carry the
  // diagnostic-only workaround flag.
  if (cmd.find("allow-multiple-definition") != std::string::npos) {
    std::printf("  detail: link command must not contain "
                "--allow-multiple-definition\n");
    return false;
  }
  if (run_cmd(cmd) != 0) {
    std::printf("  detail: app-shell shared link failed "
                "(duplicate JNI_OnLoad?)\n");
    return false;
  }
  return true;
}

// Runs `nm -D` on the linked library and returns its stdout (empty on error).
static std::string dynamic_symbols() {
  std::string out_path = g_tmpdir + "/dynsym.txt";
  std::string cmd = std::string("nm -D --defined-only \"") + g_shared_lib +
                    "\" > \"" + out_path + "\" 2>/dev/null";
  if (run_cmd(cmd) != 0) return std::string();
  return read_file(out_path);
}

// Named case: both Activity natives are exported by the app-shell link.
static bool check_both_activity_natives_exported() {
  std::string syms = dynamic_symbols();
  if (syms.empty()) {
    std::printf("  detail: nm -D produced no output (nm missing or link "
                "absent)\n");
    return false;
  }
  bool load = syms.find("Java_com_muse_host_MainActivity_nativeLoadTree") !=
              std::string::npos;
  bool render =
      syms.find("Java_com_muse_host_MainActivity_nativeRenderFrame") !=
      std::string::npos;
  if (!load)
    std::printf("  detail: missing nativeLoadTree in nm -D\n");
  if (!render)
    std::printf("  detail: missing nativeRenderFrame in nm -D\n");
  return load && render;
}

// Named case: exactly one dynamic JNI_OnLoad (`grep -c " T JNI_OnLoad"` == 1).
static bool check_exactly_one_jni_onload() {
  std::string syms = dynamic_symbols();
  if (syms.empty()) {
    std::printf("  detail: nm -D produced no output (nm missing or link "
                "absent)\n");
    return false;
  }
  std::size_t n = count_occurrences(syms, " T JNI_OnLoad");
  std::printf("  detail: \" T JNI_OnLoad\" count=%zu\n", n);
  return n == 1;
}

// Plan Interface: run_link_check() -> exit 0 iff (shared link of
// TreeBridge.cpp.o + libruntime.a with no -Wl,--allow-multiple-definition
// succeeds AND nm -D lists
// Java_com_muse_host_MainActivity_nativeLoadTree +
// Java_com_muse_host_MainActivity_nativeRenderFrame + exactly one JNI_OnLoad);
// nonzero otherwise.
static int run_link_check() {
  int failures = 0;
#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

  RUN_CASE(check_template_defines_no_jni_onload);
  RUN_CASE(check_treebridge_compiles);
  if (failures > 0) {
    std::printf("jni_bridge_link: %d failing (compile stage)\n", failures);
    return 1;
  }
  RUN_CASE(check_appshell_shared_link_succeeds);
  if (failures > 0) {
    std::printf("jni_bridge_link: %d failing (link stage)\n", failures);
    return 1;
  }
  RUN_CASE(check_both_activity_natives_exported);
  RUN_CASE(check_exactly_one_jni_onload);
#undef RUN_CASE
  if (failures > 0) {
    std::printf("jni_bridge_link: %d failing\n", failures);
    return 1;
  }
  std::printf("jni_bridge_link: all passing\n");
  return 0;
}

int main(int argc, char** argv) {
  if (argc > 1 && argv[1][0] != '\0') {
    g_repo_root = argv[1];
  }
  if (!find_repo_root()) {
    std::printf("FAIL find_repo_root\n");
    std::printf("  detail: cannot locate "
                "packages/cli/templates/android/TreeBridge.cpp (pass repo "
                "root as argv[1] or set $MUSE_REPO_ROOT)\n");
    return 1;
  }
  resolve_inputs();
  if (jni_flags().empty()) {
    std::printf("FAIL find_jni_headers\n");
    std::printf("  detail: no jni.h sysroot (pinned "
                "/usr/lib/jvm/java-21-openjdk-amd64/include* absent, "
                "$JAVA_INCLUDE/$JAVA_HOME unset)\n");
    return 1;
  }
  if (!make_tmpdir()) {
    std::printf("FAIL make_tmpdir\n");
    return 1;
  }
  std::printf("  detail: repo=%s tmp=%s engine_lib=%s\n", g_repo_root.c_str(),
              g_tmpdir.c_str(),
              g_runtime_lib.empty() ? "(jni_bridge.cpp fallback)"
                                    : g_runtime_lib.c_str());
  return run_link_check();
}
