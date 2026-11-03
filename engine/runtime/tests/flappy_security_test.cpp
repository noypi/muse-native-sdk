// Security-review pins for run bug-runtime-009-010 (security-review.md
// "Security Tests Requested", owner code-unit-tester), revised for
// BUG-RUNTIME-012: the per-app content module is deleted, so the four
// module-bound pins (unknown-kind naming, drain-empty fail-closed, store
// cap, null-data register) are deleted — see the run contract for their
// lines. What remains is the registry-driven record-seam authorization
// (handler-mismatch deny + forged-node deny) proven through a test-local
// fake, plus the removal pin (no content provider registered).
// Binds ONLY to production Real headers; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no device, no network,
// no env/config reads, no filesystem, no live services. Every collaborator
// is in-process.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "runtime/content_registry.h"
#include "runtime/presenter.h"

namespace {

// Test-local record double honoring the record-seam contract: nodes 31/32
// name distinct internal-literal handlers and record only with the exact
// handler; every other node/Handler pair denies. Registered under a test
// tag — never production content.
const char* test_touch_handler_name(std::int32_t nodeId) {
  switch (nodeId) {
    case 31:
      return "onTouchStart";
    case 32:
      return "onHudTouch";
    default:
      return nullptr;
  }
}
bool test_record_tap(std::int32_t nodeId, const char* handler) {
  if (handler == nullptr) return false;
  const char* expected = test_touch_handler_name(nodeId);
  if (expected == nullptr) return false;
  return std::string(handler) == expected;
}
bool RegisterTestRecordTag() {
  runtime::ContentHandlers handlers;
  handlers.touchHandlerName = &test_touch_handler_name;
  handlers.recordTap = &test_record_tap;
  (void)GlobalContentRegistry().Register("test-security-record", handlers);
  return GlobalContentRegistry().Resolve("test-security-record") != nullptr;
}

}  // namespace

static bool check_harness_entry_reached() { return true; }

// Removal pin (BUG-RUNTIME-012): with the content module deleted, no
// content provider answers the registry — the removed tag resolves
// nullptr and the steer node names no handler before test registration.
static bool check_removed_module_registers_nothing() {
  if (GlobalContentRegistry().Resolve("flappy") != nullptr) {
    std::printf("  detail: removed content tag still registered\n");
    return false;
  }
  if (runtime::ContentTouchHandlerName(31) != nullptr) {
    std::printf("  detail: steer node names a handler with no provider\n");
    return false;
  }
  if (runtime::FireContentTapToJs(31, "onTouchStart")) {
    std::printf("  detail: tap recorded with no provider registered\n");
    return false;
  }
  return true;
}

// 1. Handler-mismatch deny: a forged (nodeId, handler) pair returns
// false with no record — the core dispatch authorization. Cross-node
// negative case: node 31's steer handler is NOT node 32's HUD handler.
// Control: each genuine pair still records (deny is pair-scoped, not
// node-scoped).
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
    // Control: the genuine pairs still record.
    if (!runtime::FireContentTapToJs(31, h31)) {
      std::printf("  detail: genuine (31, steer) pair did not record\n");
      return false;
    }
    if (!runtime::FireContentTapToJs(32, h32)) {
      std::printf("  detail: genuine (32, hud) pair did not record\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: record seam must never throw\n");
    return false;
  }
  return true;
}

// 2. Forged/stale/zero node ids fail closed on the record seam: scrim and
// forged nodes name nothing and record nothing; wrong-handler and null
// handler deny; zero ids deny. Control: node 31 + "onTouchStart" records.
static bool check_record_seam_denies_forged_node_and_handler() {
  try {
    if (runtime::ContentTouchHandlerName(20) != nullptr) {
      std::printf("  detail: scrim node 20 named a handler\n");
      return false;
    }
    if (runtime::ContentTouchHandlerName(-999) != nullptr) {
      std::printf("  detail: forged node named a handler\n");
      return false;
    }
    if (runtime::FireContentTapToJs(20, "onTouchStart")) {
      std::printf("  detail: scrim node 20 recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(-999, "onTouchStart")) {
      std::printf("  detail: forged node recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(31, "onTouchEnd")) {
      std::printf("  detail: wrong handler recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(31, nullptr)) {
      std::printf("  detail: null handler recorded a tap\n");
      return false;
    }
    if (runtime::FireContentTapToJs(0, "onTouchStart")) {
      std::printf("  detail: zero node recorded a tap\n");
      return false;
    }
    if (!runtime::FireContentTapToJs(31, "onTouchStart")) {
      std::printf("  detail: control node 31 + onTouchStart did not record\n");
      return false;
    }
  } catch (...) {
    std::printf("  detail: record seam must never throw\n");
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
  RUN_CASE(check_harness_entry_reached);
  // Removal pin runs before any test registration: the registry must be
  // empty of content providers in a fresh process.
  RUN_CASE(check_removed_module_registers_nothing);
  if (!RegisterTestRecordTag()) {
    std::printf("FAIL test_record_tag_registration\n");
    std::printf("flappy_security_test: 1 failing\n");
    return 1;
  }
  std::printf("PASS test_record_tag_registration\n");
  RUN_CASE(check_handler_mismatch_deny);
  RUN_CASE(check_record_seam_denies_forged_node_and_handler);
  if (failures > 0) {
    std::printf("flappy_security_test: %d failing\n", failures);
    return 1;
  }
  std::printf("flappy_security_test: all passing\n");
  return 0;
}
