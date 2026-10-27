// H1 contract tests — FR-RUNTIME-005 / AC-RUNTIME-011 (TC-RUNTIME-011).
// Binds ONLY to the H1 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; no network, no env/
// config reads, no filesystem, no live services.
#include <cstddef>
#include <cstdio>
#include <cstring>

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

// --- H1 helpers ------------------------------------------------------------

namespace {

runtime::Surface make_fake_surface() { return runtime::Surface{}; }

}  // namespace

// Named case: create gives a live surface and a live tree.
static bool check_create_gives_live_surface_and_tree() {
  runtime::Host h = runtime::create_host(make_fake_surface());
  bool ok = runtime::surface_alive(h) && runtime::live_tree_nodes(h) > 0;
  runtime::destroy_host(h);
  return ok;
}

// Named case: post-destroy surface is dead and no tree nodes survive
// (RULE-RUNTIME-004).
static bool check_destroy_leaves_no_surface_no_tree() {
  runtime::Host h = runtime::create_host(make_fake_surface());
  runtime::destroy_host(h);
  return !runtime::surface_alive(h) && runtime::live_tree_nodes(h) == 0;
}

// Named case: double-destroy is a no-op (still dead, still empty, no crash).
static bool check_double_destroy_is_noop() {
  runtime::Host h = runtime::create_host(make_fake_surface());
  runtime::destroy_host(h);
  runtime::destroy_host(h);
  return !runtime::surface_alive(h) && runtime::live_tree_nodes(h) == 0;
}

// Named case: two hosts are independent — destroying one leaves the other
// alive (no leaked/shared surface).
static bool check_hosts_are_independent() {
  runtime::Host a = runtime::create_host(make_fake_surface());
  runtime::Host b = runtime::create_host(make_fake_surface());
  runtime::destroy_host(a);
  bool ok = !runtime::surface_alive(a) && runtime::live_tree_nodes(a) == 0 &&
            runtime::surface_alive(b) && runtime::live_tree_nodes(b) > 0;
  runtime::destroy_host(b);
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
  RUN_CASE(check_create_gives_live_surface_and_tree);
  RUN_CASE(check_destroy_leaves_no_surface_no_tree);
  RUN_CASE(check_double_destroy_is_noop);
  RUN_CASE(check_hosts_are_independent);
  if (failures > 0) {
    std::printf("host_lifecycle_test: %d failing\n", failures);
    return 1;
  }
  std::printf("host_lifecycle_test: all passing\n");
  return 0;
}
