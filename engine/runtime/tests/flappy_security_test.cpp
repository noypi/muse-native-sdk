// Security-review pins for run bug-runtime-009-010 (security-review.md
// "Security Tests Requested", 5 rows, owner code-unit-tester).
// Binds ONLY to production Real headers; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no device, no network,
// no env/config reads, no filesystem, no live services. Every collaborator
// is in-process: content bytes are inline literals, the swapchain is an
// in-memory fake, JsRuntime dispatch runs through the Real factory graph
// in-process. Real-only: assertions bind the Real registry/JsRuntime/drain
// path (no Fake stands in for production behavior).
//
// Ordering note: the null-data case runs LAST in main() — before the coder
// lands the LOW-note guard it faults (SIGSEGV, uncatchable by design), so
// it must not mask the other four cases.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/flappy_content.h"
#include "runtime/gallery_present.h"
#include "runtime/presenter.h"
#include "runtime/renderer.h"

// Production dispatch surface (engine/runtime/src/flappy_content.cpp,
// global namespace): declared here against the header/registry contract;
// the definition is Real production code linked into this binary.
struct JsDispatchResult {
  bool fired = false;
  std::int32_t nodeId = -1;
  bool jsHandled = false;
};
JsDispatchResult DispatchTapViaJsRuntime(std::int32_t nodeId,
                                         const char* handler);

namespace {

// In-process fake swapchain: records every submitted cmd. No GPU.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

// Swapchain whose submit always throws: Flush catches per-cmd, so the
// drained count stays 0 — the submitted==0 fail-closed leg. Never escapes.
class ThrowingSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd&) override { throw 1; }
};

runtime::Renderer test_renderer() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return runtime::Renderer(caps);
}

TreeHandle RegisterLiteral(const std::string& bytes) {
  return RegisterFlappyContentTree(bytes.data(), bytes.size());
}

// Shared fixtures, registered ONCE before the cap test consumes the store.
// Every case below reuses these handles/bytes (re-registration refreshes in
// place and never counts against the cap); no case registers new distinct
// bytes except the cap probe itself.
const std::string& FixtureValidBytes() {
  static const std::string bytes =
      "FlappyTopAppBar FlappyHud FlappyBird FlappyPipe FlappyScore "
      "FlappyGameOver security-pin";
  return bytes;
}
const std::string& FixtureExtraBytes() {
  static const std::string bytes = "FlappyBird FlappyPipe FlappyBirdExtra";
  return bytes;
}
TreeHandle g_validTree = 0;
TreeHandle g_extraTree = 0;
bool RegisterFixtures() {
  g_validTree = RegisterLiteral(FixtureValidBytes());
  g_extraTree = RegisterLiteral(FixtureExtraBytes());
  return g_validTree != 0 && g_extraTree != 0 && g_validTree != g_extraTree;
}

}  // namespace

static bool check_harness_entry_reached() { return true; }

// 1. Handler-mismatch deny: a forged (nodeId, handler) pair returns
// fired=false with no eval — the core dispatch authorization. Cross-node
// negative case: node 31's steer handler is NOT node 32's HUD handler.
static bool check_handler_mismatch_deny() {
  try {
    const char* h31 = runtime::ContentTouchHandlerName(31);
    const char* h32 = runtime::ContentTouchHandlerName(32);
    if (h31 == nullptr || h32 == nullptr) {
      std::printf("  detail: touchables 31/32 name no handlers\n");
      return false;
    }
    // Forged pair on the record seam: wrong handler for a valid node.
    if (runtime::FireContentTapToJs(31, h32)) {
      std::printf("  detail: node 31 recorded with node 32's handler\n");
      return false;
    }
    if (runtime::FireContentTapToJs(32, h31)) {
      std::printf("  detail: node 32 recorded with node 31's handler\n");
      return false;
    }
    // Forged pair on the JsRuntime dispatch seam: must not fire, must not
    // reach eval (fired=false is returned before any eval attempt).
    const JsDispatchResult r = DispatchTapViaJsRuntime(31, h32);
    if (r.fired || r.jsHandled) {
      std::printf("  detail: forged (31, hud-handler) pair dispatched\n");
      return false;
    }
    // Control: the genuine pair still fires (deny is pair-scoped, not
    // node-scoped).
    const JsDispatchResult ok = DispatchTapViaJsRuntime(31, h31);
    if (!ok.fired) {
      std::printf("  detail: genuine (31, registered) pair did not fire\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: dispatch seam must never throw\n");
    return false;
  }
  return true;
}

// 2. Unknown-kind fail-closed with name: a `FlappyBirdExtra`-style
// suffixed token yields unknownKindCount>0 with the FULL token named
// (RULE-RUNTIME-001 pin; prefix truncation would hide the attacker token).
static bool check_unknown_kind_suffixed_token_named() {
  try {
    (void)runtime::RegisterFlappyContent();
    // Fixture handle (registered before the cap test filled the store).
    const FrameStats stats = renderTree(g_extraTree);
    if (stats.unknownKindCount <= 0) {
      std::printf("  detail: suffixed token swallowed (unknownKindCount 0)\n");
      return false;
    }
    if (stats.unknownKindName != "FlappyBirdExtra") {
      std::printf("  detail: expected unknownKindName FlappyBirdExtra, got %s\n",
                  stats.unknownKindName.c_str());
      return false;
    }
  } catch (...) {
    std::printf("  detail: render seam must never throw\n");
    return false;
  }
  return true;
}

// 3. Drain-empty fail-closed: submitted==0 (every submit throws, so the
// drain loop submits nothing) makes the 5-arg present return false with a
// non-blank diagnostic — never a blank-viewport present.
static bool check_drain_empty_fail_closed() {
  try {
    (void)runtime::RegisterFlappyContent();
    // Fixture bytes: re-registration refreshes the known handle in place,
    // never a new store entry (safe to run after the cap test filled it).
    const std::string& scene = FixtureValidBytes();
    runtime::Renderer renderer = test_renderer();
    ThrowingSwapchain target;
    std::string error;
    const bool ok = runtime::TryPresentContentScene(scene.data(), scene.size(),
                                                   renderer, target, &error);
    if (ok) {
      std::printf("  detail: zero-submit drain presented true\n");
      return false;
    }
    if (error.empty()) {
      std::printf("  detail: zero-submit drain left no diagnostic\n");
      return false;
    }
    // Control: the same scene drains clean into a recording target.
    FakeSwapchain good;
    std::string goodError;
    const bool goodOk = runtime::TryPresentContentScene(
        scene.data(), scene.size(), renderer, good, &goodError);
    if (!goodOk || !goodError.empty() || good.captured.empty()) {
      std::printf("  detail: control scene did not drain clean\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: present seam must never throw\n");
    return false;
  }
  return true;
}

// 4. Store cap: once 32 distinct payloads are retained process-wide, a
// further distinct payload fails closed (returns 0, no insert, no eviction)
// while known payloads still refresh in place. Two fixture trees are
// already retained by RegisterFixtures(), so this probe adds exactly 30
// more; the overflow probe is the 33rd distinct payload overall.
static bool check_store_cap_fails_closed_no_evict() {
  try {
    (void)runtime::RegisterFlappyContent();
    std::vector<TreeHandle> handles;
    std::string firstPayload;
    TreeHandle firstHandle = 0;
    for (int i = 0; i < 128 && handles.size() < 30; ++i) {
      const std::string payload =
          "FlappyBird FlappyPipe store-cap-pin variant-" + std::to_string(i);
      const TreeHandle h = RegisterLiteral(payload);
      if (h == 0) {
        std::printf("  detail: store capped at %zu probe trees, want 30\n",
                    handles.size());
        return false;
      }
      bool seen = false;
      for (const TreeHandle known : handles) {
        if (known == h) {
          seen = true;
          break;
        }
      }
      if (seen) {
        continue;  // hash refresh-collision: probe the next payload.
      }
      handles.push_back(h);
      if (handles.size() == 1) {
        firstPayload = payload;
        firstHandle = h;
      }
    }
    if (handles.size() != 30) {
      std::printf("  detail: expected 30 probe trees (32 with fixtures), got %zu\n",
                  handles.size());
      return false;
    }
    // Next distinct payload must fail closed: the 33rd distinct tree.
    const TreeHandle overflowHandle = RegisterLiteral(
        "FlappyBird FlappyPipe store-cap-pin overflow-probe");
    if (overflowHandle != 0) {
      std::printf("  detail: 33rd distinct payload registered (no cap)\n");
      return false;
    }
    // No eviction: the first probe payload AND both fixtures still refresh
    // to their own handles.
    if (RegisterLiteral(firstPayload) != firstHandle) {
      std::printf("  detail: capped store evicted or moved a known handle\n");
      return false;
    }
    if (RegisterLiteral(FixtureValidBytes()) != g_validTree ||
        RegisterLiteral(FixtureExtraBytes()) != g_extraTree) {
      std::printf("  detail: capped store evicted a fixture handle\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: register seam must never throw\n");
    return false;
  }
  return true;
}

// 5. Null-data fail-closed on the register path (security-review LOW note):
// RegisterFlappyContentTree(nullptr, N>0) returns 0 without crashing.
// RUNS LAST: pre-fix this faults the process (SIGSEGV is uncatchable by
// design), which is the RED evidence for the coder's guard.
static bool check_register_null_data_fail_closed() {
  try {
    const TreeHandle h =
        RegisterFlappyContentTree(nullptr, static_cast<std::size_t>(64));
    if (h != 0) {
      std::printf("  detail: null data registered nonzero handle\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: register seam must never throw\n");
    return false;
  }
  return true;
}

#define RUN_CASE(name)                               \
  do {                                               \
    if (!(name)()) {                                 \
      std::printf("FAIL %s\n", #name);                \
      ++failures;                                    \
    } else {                                         \
      std::printf("PASS %s\n", #name);                \
    }                                                \
  } while (0)

int main() {
  int failures = 0;
  (void)runtime::RegisterFlappyContent();
  RUN_CASE(check_harness_entry_reached);
  if (!RegisterFixtures()) {
    std::printf("FAIL fixture_registration\n");
    std::printf("flappy_security_test: 1 failing\n");
    return 1;
  }
  std::printf("PASS fixture_registration\n");
  RUN_CASE(check_store_cap_fails_closed_no_evict);
  RUN_CASE(check_handler_mismatch_deny);
  RUN_CASE(check_unknown_kind_suffixed_token_named);
  RUN_CASE(check_drain_empty_fail_closed);
  RUN_CASE(check_register_null_data_fail_closed);
  if (failures > 0) {
    std::printf("flappy_security_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_security_test: all passing\n");
  return 0;
}
