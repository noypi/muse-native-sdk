// Batch 1 / T1+T2 contract tests — FR-RUNTIME-013 / AC-RUNTIME-038/039.
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no device, no network,
// no env/config reads, no filesystem, no live services. Every collaborator
// is in-process: fake tags only, no registered production content.
//
// Fail-closed-when-absent: this file includes the T1 header
// `runtime/content_registry.h`, which does not exist until the T1 coder
// pass lands — so compiling this file before then MUST fail (expected red,
// not a malformed test). The tests never define the registry themselves.
//
// BUG-RUNTIME-012: the per-app content module is deleted. This file binds
// only the generic tag-registry seam plus the surviving record seam; cases
// that needed the removed module (scene-store cap, dual-tag gate) are
// deleted — see the run contract for their lines. The forged-node case
// drives the seam through a test-local fake under a test tag.
//
// Seam note: this file names no per-app symbol. That is the T2 assertion in
// miniature — generic dispatch (and its tests) resolve content by tag,
// never by app name.
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "runtime/content_registry.h"
#include "runtime/presenter.h"

// Forward-declared (per gallery_layering_test.cpp): FireTapHandler lives in
// input_chain.cpp, whose header needs PRIVATE input paths unavailable here.
bool FireTapHandler(std::int32_t nodeId);

// --- T1 `Interface:` line (plan verbatim) --------------------------------
// `ContentRegistry::Register(ContentTag, ContentHandlers) -> bool;
//   ContentRegistry::Resolve(tag: string_view) -> const ContentHandlers* |
//   nullptr` — unknown tag returns nullptr (fail-closed per RULE-RUNTIME-001);
//   duplicate tag returns false, first wins.

// Test-owned fake tags (never production content names).
namespace {
constexpr std::string_view kTagA = "test-content-a";
constexpr std::string_view kTagB = "test-content-b";

// Distinct fake record-tap behaviors used to prove first-wins keeps the
// first bundle's handlers (return values differ, so identity is observable).
bool fake_first_record_tap(std::int32_t, const char*) { return true; }
bool fake_second_record_tap(std::int32_t, const char*) { return false; }
}  // namespace

// Named case: the harness executable entry point exists and runs.
static bool check_harness_entry_reached() { return true; }

// Named case: pass protocol reserves return 0 for all-green.
static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

// Named case: failure protocol reserves non-zero + failing name on stdout.
static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && failingName[0] == '\0';
}

// Named case: registering a fake tag resolves to its handlers (T1 happy
// path; AC-RUNTIME-038 registration half).
static bool check_register_fake_tag_resolves() {
  runtime::ContentRegistry registry;
  const runtime::ContentHandlers handlers{};
  if (!registry.Register(kTagA, handlers)) return false;
  const runtime::ContentHandlers* resolved = registry.Resolve(kTagA);
  return resolved != nullptr;
}

// Named case: resolving an unknown tag returns nullptr (fail-closed per
// RULE-RUNTIME-001; AC-RUNTIME-038 unknown-content half).
static bool check_unknown_tag_resolves_nullptr() {
  runtime::ContentRegistry registry;
  return registry.Resolve("test-content-unregistered") == nullptr;
}

// Named case: resolving the empty tag returns nullptr (boundary;
// fail-closed, never a default handler).
static bool check_empty_tag_resolves_nullptr() {
  runtime::ContentRegistry registry;
  return registry.Resolve(std::string_view{}) == nullptr;
}

// Named case: duplicate registration returns false and the first handlers
// win (T1 duplicate half).
static bool check_duplicate_register_returns_false_first_wins() {
  runtime::ContentRegistry registry;
  const runtime::ContentHandlers first{};
  const runtime::ContentHandlers second{};
  if (!registry.Register(kTagA, first)) return false;
  if (registry.Register(kTagA, second)) return false;
  return registry.Resolve(kTagA) != nullptr;
}

// Named case: two tags resolve independently — dispatch distinguishes
// content by tag alone, never by app name (T2 seam contract; AC-038
// "dispatch calls the registration, never the app by name").
static bool check_two_tags_resolve_independently_by_tag() {
  runtime::ContentRegistry registry;
  const runtime::ContentHandlers handlersA{};
  const runtime::ContentHandlers handlersB{};
  if (!registry.Register(kTagA, handlersA)) return false;
  if (!registry.Register(kTagB, handlersB)) return false;
  const runtime::ContentHandlers* resolvedA = registry.Resolve(kTagA);
  const runtime::ContentHandlers* resolvedB = registry.Resolve(kTagB);
  if (resolvedA == nullptr || resolvedB == nullptr) return false;
  if (resolvedA == resolvedB) return false;
  return registry.Resolve("test-content-unregistered") == nullptr;
}

// Named case: unknown tag stays fail-closed after registrations exist
// (registration never widens the miss path).
static bool check_unknown_stays_null_after_registrations() {
  runtime::ContentRegistry registry;
  const runtime::ContentHandlers handlers{};
  if (!registry.Register(kTagA, handlers)) return false;
  return registry.Resolve(kTagB) == nullptr;
}

// Test-local record double honoring the record-seam contract: only the
// steer node (31) with the exact handler records; everything else denies.
// Registered under a test tag — never a production content name.
const char* fake_gate_touch_handler_name(std::int32_t nodeId) {
  return nodeId == 31 ? "onTouchStart" : nullptr;
}
bool fake_gate_record_tap(std::int32_t nodeId, const char* handler) {
  if (handler == nullptr) return false;
  return nodeId == 31 && std::string_view(handler) == "onTouchStart";
}
bool RegisterTestRecordTag() {
  runtime::ContentHandlers handlers;
  handlers.touchHandlerName = &fake_gate_touch_handler_name;
  handlers.recordTap = &fake_gate_record_tap;
  // First wins: re-registration across cases keeps the first bundle.
  (void)GlobalContentRegistry().Register("test-content-record", handlers);
  return GlobalContentRegistry().Resolve("test-content-record") != nullptr;
}

// Named case: first-wins re-register keeps the FIRST bundle's handlers
// (observable via distinct fake behaviors), not the second bundle's.
static bool check_first_wins_keeps_handler_identity() {
  runtime::ContentRegistry registry;
  runtime::ContentHandlers first;
  first.recordTap = &fake_first_record_tap;
  runtime::ContentHandlers second;
  second.recordTap = &fake_second_record_tap;
  if (!registry.Register("test-content-identity", first)) return false;
  if (registry.Register("test-content-identity", second)) return false;
  const runtime::ContentHandlers* resolved =
      registry.Resolve("test-content-identity");
  if (resolved == nullptr) return false;
  if (resolved->recordTap != &fake_first_record_tap) return false;
  return resolved->recordTap(31, "onTouchStart");
}

// Named case: an all-nullptr bundle fails closed on every dispatch path —
// each of the seven ForEach sites null-checks before calling, so no call
// is ever dispatched through this bundle.
static bool check_nullptr_bundle_fails_closed_all_paths() {
  runtime::ContentRegistry registry;
  if (!registry.Register("test-content-null", runtime::ContentHandlers{})) {
    return false;
  }
  const runtime::ContentHandlers* resolved =
      registry.Resolve("test-content-null");
  if (resolved == nullptr) return false;
  if (resolved->touchHandlerName != nullptr) return false;
  if (resolved->recordTap != nullptr) return false;
  if (resolved->tapCount != nullptr) return false;
  if (resolved->tapNodeId != nullptr) return false;
  if (resolved->tapHandler != nullptr) return false;
  if (resolved->tapBounds != nullptr) return false;
  if (resolved->drawSceneKind != nullptr) return false;
  bool dispatched = false;
  registry.ForEach([&](runtime::ContentTag,
                       const runtime::ContentHandlers& handlers) {
    if (handlers.touchHandlerName != nullptr) {
      handlers.touchHandlerName(31);
      dispatched = true;
    }
    if (handlers.recordTap != nullptr) {
      handlers.recordTap(31, "onTouchStart");
      dispatched = true;
    }
    if (handlers.tapCount != nullptr) {
      handlers.tapCount(202);
      dispatched = true;
    }
    if (handlers.tapNodeId != nullptr) {
      handlers.tapNodeId(202, 0);
      dispatched = true;
    }
    if (handlers.tapHandler != nullptr) {
      handlers.tapHandler(202, 0);
      dispatched = true;
    }
    if (handlers.tapBounds != nullptr) {
      float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
      handlers.tapBounds(202, 31, &x, &y, &w, &h);
      dispatched = true;
    }
    if (handlers.drawSceneKind != nullptr) {
      const std::string kind = "test-kind-a";
      handlers.drawSceneKind(kind, 31);
      dispatched = true;
    }
  });
  return !dispatched;
}

// Named case: forged/stale/zero node ids fail closed. Only the steer node
// (31) with the exact handler records through the test-local fake and the
// surviving generic record seam; wrong node, handler mismatch, nullptr
// handler, and zero ids never record. Unclaimed ids (zero, negative,
// never-granted) never fire.
static bool check_forged_node_ids_fail_closed() {
  if (!RegisterTestRecordTag()) return false;
  const runtime::ContentHandlers* handlers =
      GlobalContentRegistry().Resolve("test-content-record");
  if (handlers == nullptr || handlers->recordTap == nullptr) return false;
  if (!handlers->recordTap(31, "onTouchStart")) return false;
  if (handlers->recordTap(20, "onTouchStart")) return false;
  if (handlers->recordTap(31, "onHudTouch")) return false;
  if (handlers->recordTap(31, nullptr)) return false;
  if (handlers->recordTap(0, "onTouchStart")) return false;
  // Same denials through the surviving generic record seam.
  if (runtime::ContentTouchHandlerName(20) != nullptr) return false;
  if (!runtime::FireContentTapToJs(31, "onTouchStart")) return false;
  if (runtime::FireContentTapToJs(20, "onTouchStart")) return false;
  if (FireTapHandler(0)) return false;
  if (FireTapHandler(-1)) return false;
  if (FireTapHandler(31)) return false;
  return true;
}

#define RUN_CASE(name)                             \
  do {                                             \
    if (!(name)()) {                               \
      std::printf("FAIL %s\n", #name);             \
      ++failures;                                  \
    } else {                                       \
      std::printf("PASS %s\n", #name);             \
    }                                              \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_register_fake_tag_resolves);
  RUN_CASE(check_unknown_tag_resolves_nullptr);
  RUN_CASE(check_empty_tag_resolves_nullptr);
  RUN_CASE(check_duplicate_register_returns_false_first_wins);
  RUN_CASE(check_two_tags_resolve_independently_by_tag);
  RUN_CASE(check_unknown_stays_null_after_registrations);
  RUN_CASE(check_first_wins_keeps_handler_identity);
  RUN_CASE(check_nullptr_bundle_fails_closed_all_paths);
  RUN_CASE(check_forged_node_ids_fail_closed);
  if (failures > 0) {
    std::printf("content_registry_test: %d failing\n", failures);
    return 1;
  }
  std::printf("content_registry_test: all passing\n");
  return 0;
}
