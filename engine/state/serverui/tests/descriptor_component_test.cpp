// Batch 4 (Server-Driven UI, FR-STATE-009) contract test — T8.
// Spec refs: FR-STATE-009, AC-STATE-036 (TC-STATE-036).
// Hermetic: C++17 + committed engine/state headers only; no env/config/network.
// Binds ONLY to the T8 `Interface:` line; never to implementation.
//
// Structural ASSUMPTIONS (see unit-tests/T8.md):
//   A-S1: namespace muse::state; all types live in `muse/state/serverui.hpp`.
//   A-S2: Descriptor is a list of named nodes: exposes `void addNode(const char*,
//         const Props&)` (or equivalent push), `std::size_t nodeCount() const`,
//         `const char* nodeName(std::size_t) const`, `const Props&
//         nodeProps(std::size_t) const`.
//   A-S3: Props is default-constructible and equality-comparable.
//   A-S4: Component exposes `bool isPlaceholder() const`; ComponentTree exposes
//         `static ComponentTree placeholder()`, `std::size_t size() const`,
//         `const Component& at(std::size_t) const`, `bool isPlaceholder() const`
//         (true only for a fully-placeholder tree).
//   A-S5: ComponentFactory is `StatusOr<Component>(const Props&)` (any callable
//         with that call shape; e.g. std::function).
//   A-S6: registerComponent(nullptr, f) and unknown inflate paths return
//         non-kOk Status / placeholder, never throw; duplicate registration of
//         the same name replaces the factory and returns kOk.
#include <cstdio>
#include <string>

#include "muse/state/atom.hpp"
#include "muse/state/serverui.hpp"

namespace {

using muse::state::Component;
using muse::state::ComponentFactory;
using muse::state::ComponentTree;
using muse::state::Descriptor;
using muse::state::Props;
using muse::state::ServerUiRegistry;
using muse::state::Status;

static Component makeReal(const Props& /*p*/) { return Component(); }

// Known component inflates to a real (non-placeholder) tree entry.
static bool check_known_component_inflates() {
  ServerUiRegistry r;
  if (r.registerComponent("Button", makeReal) != Status::kOk) return false;
  Descriptor d;
  d.addNode("Button", Props());
  auto t = r.inflate(d);
  if (!t.ok()) return false;
  if (t.value().size() != 1) return false;
  return !t.value().at(0).isPlaceholder();
}

// Unknown name inflates that subtree to placeholder; rest still renders.
static bool check_unknown_component_degrades_to_placeholder_rest_renders() {
  ServerUiRegistry r;
  if (r.registerComponent("Button", makeReal) != Status::kOk) return false;
  Descriptor d;
  d.addNode("Button", Props());
  d.addNode("MysteryWidget", Props());
  auto t = r.inflate(d);
  if (!t.ok()) return false;
  if (t.value().size() != 2) return false;
  if (t.value().at(0).isPlaceholder()) return false;
  return t.value().at(1).isPlaceholder();
}

// All-unknown descriptor: every entry is a placeholder, inflate still kOk.
static bool check_all_unknown_inflates_to_placeholders() {
  ServerUiRegistry r;
  Descriptor d;
  d.addNode("Nope", Props());
  d.addNode("AlsoNope", Props());
  auto t = r.inflate(d);
  if (!t.ok()) return false;
  if (t.value().size() != 2) return false;
  return t.value().at(0).isPlaceholder() && t.value().at(1).isPlaceholder();
}

// Empty descriptor inflates to an empty tree (kOk, size 0).
static bool check_empty_descriptor_inflates_empty() {
  ServerUiRegistry r;
  Descriptor d;
  auto t = r.inflate(d);
  return t.ok() && t.value().size() == 0;
}

// Registering the same name twice replaces the factory and returns kOk.
static bool check_duplicate_registration_replaces_factory() {
  ServerUiRegistry r;
  if (r.registerComponent("Button", makeReal) != Status::kOk) return false;
  ComponentFactory placeholderFactory = [](const Props& /*p*/)
      -> muse::state::StatusOr<Component> { return ComponentTree::placeholder().at(0); };
  if (r.registerComponent("Button", placeholderFactory) != Status::kOk) return false;
  Descriptor d;
  d.addNode("Button", Props());
  auto t = r.inflate(d);
  return t.ok() && t.value().size() == 1 && t.value().at(0).isPlaceholder();
}

// Null name registration is rejected with non-kOk, never throws.
static bool check_null_name_registration_rejected() {
  ServerUiRegistry r;
  return r.registerComponent(nullptr, makeReal) != Status::kOk;
}

// Factory error propagates: inflate returns non-kOk for that descriptor.
static bool check_factory_error_propagates() {
  ServerUiRegistry r;
  ComponentFactory failing = [](const Props& /*p*/)
      -> muse::state::StatusOr<Component> { return Status::kCorrupt; };
  if (r.registerComponent("Broken", failing) != Status::kOk) return false;
  Descriptor d;
  d.addNode("Broken", Props());
  return !r.inflate(d).ok();
}

// Null/empty factory registration is rejected with non-kOk, never throws;
// inflate is unaffected (unknown name still degrades to placeholder, kOk).
static bool check_null_factory_registration_rejected_inflate_unaffected() {
  ServerUiRegistry r;
  ComponentFactory empty;
  if (r.registerComponent("X", empty) == Status::kOk) return false;
  if (r.registerComponent("X", nullptr) == Status::kOk) return false;
  Descriptor d;
  d.addNode("X", Props());
  auto t = r.inflate(d);
  if (!t.ok()) return false;
  return t.value().size() == 1 && t.value().at(0).isPlaceholder();
}

}  // namespace

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_known_component_inflates);
  RUN_CASE(check_unknown_component_degrades_to_placeholder_rest_renders);
  RUN_CASE(check_all_unknown_inflates_to_placeholders);
  RUN_CASE(check_empty_descriptor_inflates_empty);
  RUN_CASE(check_duplicate_registration_replaces_factory);
  RUN_CASE(check_null_name_registration_rejected);
  RUN_CASE(check_null_factory_registration_rejected_inflate_unaffected);
  RUN_CASE(check_factory_error_propagates);
  if (failures > 0) {
    std::printf("descriptor_component_test: %d failing\n", failures);
    return 1;
  }
  std::printf("descriptor_component_test: all passing\n");
  return 0;
}
