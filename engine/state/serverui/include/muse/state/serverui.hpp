#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "muse/state/atom.hpp"
#include "muse/state/descriptor.hpp"

namespace muse {
namespace state {

// Single inflated UI element. Default-constructed components are real
// (non-placeholder); placeholder entries are produced only for unknown
// descriptor names (or by factories that return them).
class Component {
 public:
  Component() : placeholder_(false) {}
  explicit Component(bool is_placeholder) : placeholder_(is_placeholder) {}

  bool isPlaceholder() const { return placeholder_; }

 private:
  bool placeholder_;
};

// Ordered inflate result. isPlaceholder() is true only for a non-empty tree
// whose every entry is a placeholder.
class ComponentTree {
 public:
  ComponentTree() = default;

  static ComponentTree placeholder() {
    ComponentTree tree;
    tree.components_.emplace_back(true);
    return tree;
  }

  std::size_t size() const { return components_.size(); }
  const Component& at(std::size_t index) const {
    return components_.at(index);
  }
  bool isPlaceholder() const {
    if (components_.empty()) return false;
    for (const auto& c : components_) {
      if (!c.isPlaceholder()) return false;
    }
    return true;
  }

  void pushBack(Component c) { components_.push_back(c); }

 private:
  std::vector<Component> components_;
};

// NOTE (T1.10): canonical `muse::state::Descriptor` (envelope + node/op
// surface) is owned SOLELY by `muse/state/descriptor.hpp`, included above.
// This header MUST NOT redefine Descriptor (or Props).

// Factory shape: takes node props, returns the component or a Status error.
// Only factories installed via ServerUiRegistry::registerComponent run;
// there is never dlopen/name-to-address resolution.
using ComponentFactory =
    std::function<StatusOr<Component>(const Props&)>;

// Registry of C++ components available to server descriptors. Unknown names
// inflate to placeholder entries; the rest of the descriptor still renders.
class ServerUiRegistry {
 public:
  ServerUiRegistry() = default;

  Status registerComponent(const char* name, ComponentFactory f);
  StatusOr<ComponentTree> inflate(const Descriptor& d) const;

 private:
  std::unordered_map<std::string, ComponentFactory> factories_;
};

// T11 (FR-STATE-009, AC-STATE-039): tamper fallback + cached first-frame
// budget. Canonical Descriptor/DescriptorBytes/TrustAnchor resolve via
// descriptor.hpp (included above); nothing is redefined here.
StatusOr<ComponentTree> renderWithFallback(const DescriptorBytes& b,
                                           const TrustAnchor& a,
                                           const ComponentTree& bundled);
std::uint64_t firstFrameBudgetMs();
// Cached-path cost probe in the same unit as firstFrameBudgetMs(): the
// number of nodes the cached descriptor inflates synchronously.
std::uint64_t renderCostModelNodes();

// Test-only builders (fake verifier key, never real Keystore): Nodes/Ops
// variants wrap signTestOnly with a node/op-list payload; tamperTestOnly
// flips payload bits after signing so verification fails.
DescriptorBytes signTestOnlyNodes(const char* nodes, const char* key_id,
                                  std::uint64_t valid_until,
                                  std::uint64_t version);
DescriptorBytes signTestOnlyOps(const char* ops, const char* key_id,
                                std::uint64_t valid_until,
                                std::uint64_t version);
DescriptorBytes tamperTestOnly(const DescriptorBytes& good);

}  // namespace state
}  // namespace muse
