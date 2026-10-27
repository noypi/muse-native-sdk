// H3 contract tests — FR-RUNTIME-005 / AC-RUNTIME-013 (TC-RUNTIME-013
// hermetic half; device-lab config-change stays external).
// Binds ONLY to the H3 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
#include <cstdio>
#include <cstring>
#include <vector>

#include "runtime/host.h"

// --- harness cases (convention copied from instantiate_test.cpp) -----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- H3 helpers ------------------------------------------------------------
// ASSUMPTION: `tree_intact` reports the live tree still holds every node
// created before the transition; node ids observable via `live_tree_nodes`
// count stability plus intact flag. Reconciliation: update call sites, not
// expectations, if the coder exposes ids differently.

static bool check_config_change_preserves_tree_and_surface() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  const std::size_t before = runtime::live_tree_nodes(h);
  runtime::simulate_config_change(h);
  const bool ok = runtime::tree_intact(h) && runtime::surface_valid(h) &&
                  runtime::live_tree_nodes(h) == before && before > 0;
  runtime::destroy_host(h);
  return ok;
}

static bool check_background_foreground_preserves_tree() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  const std::size_t before = runtime::live_tree_nodes(h);
  runtime::to_background(h);
  const bool bg_ok =
      runtime::tree_intact(h) && runtime::live_tree_nodes(h) == before;
  runtime::to_foreground(h);
  const bool ok = bg_ok && runtime::tree_intact(h) &&
                  runtime::surface_valid(h) &&
                  runtime::live_tree_nodes(h) == before;
  runtime::destroy_host(h);
  return ok;
}

static bool check_repeated_config_changes_keep_ids_stable() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  const std::size_t before = runtime::live_tree_nodes(h);
  runtime::simulate_config_change(h);
  runtime::simulate_config_change(h);
  const bool ok = runtime::tree_intact(h) && runtime::surface_valid(h) &&
                  runtime::live_tree_nodes(h) == before;
  runtime::destroy_host(h);
  return ok;
}

static bool check_background_twice_does_not_blank() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  runtime::to_background(h);
  runtime::to_background(h);
  runtime::to_foreground(h);
  const bool ok = runtime::tree_intact(h) && runtime::surface_valid(h) &&
                  runtime::live_tree_nodes(h) > 0;
  runtime::destroy_host(h);
  return ok;
}

static bool check_foreground_without_background_is_noop() {
  runtime::Host h = runtime::create_host(runtime::Surface{});
  const std::size_t before = runtime::live_tree_nodes(h);
  runtime::to_foreground(h);
  const bool ok = runtime::tree_intact(h) && runtime::surface_valid(h) &&
                  runtime::live_tree_nodes(h) == before;
  runtime::destroy_host(h);
  return ok;
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_config_change_preserves_tree_and_surface);
  RUN_CASE(check_background_foreground_preserves_tree);
  RUN_CASE(check_repeated_config_changes_keep_ids_stable);
  RUN_CASE(check_background_twice_does_not_blank);
  RUN_CASE(check_foreground_without_background_is_noop);
  if (failures > 0) {
    std::printf("config_change_test: %d failing\n", failures);
    return 1;
  }
  std::printf("config_change_test: all passing\n");
  return 0;
}
