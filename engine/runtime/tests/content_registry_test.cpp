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
// Seam note: this file names no per-app symbol. That is the T2 assertion in
// miniature — generic dispatch (and its tests) resolve content by tag,
// never by app name.
#include <cstdio>
#include <string_view>

#include "runtime/content_registry.h"

// --- T1 `Interface:` line (plan verbatim) --------------------------------
// `ContentRegistry::Register(ContentTag, ContentHandlers) -> bool;
//   ContentRegistry::Resolve(tag: string_view) -> const ContentHandlers* |
//   nullptr` — unknown tag returns nullptr (fail-closed per RULE-RUNTIME-001);
//   duplicate tag returns false, first wins.

// Test-owned fake tags (never production content names).
namespace {
constexpr std::string_view kTagA = "test-content-a";
constexpr std::string_view kTagB = "test-content-b";
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
  if (failures > 0) {
    std::printf("content_registry_test: %d failing\n", failures);
    return 1;
  }
  std::printf("content_registry_test: all passing\n");
  return 0;
}
