#include "muse/state/serverui.hpp"

namespace muse {
namespace state {

Status ServerUiRegistry::registerComponent(const char* name,
                                           ComponentFactory f) {
  if (name == nullptr || !f) return Status::kNotFound;
  factories_[std::string(name)] = f;
  return Status::kOk;
}

StatusOr<ComponentTree> ServerUiRegistry::inflate(const Descriptor& d) const {
  ComponentTree tree;
  for (std::size_t i = 0; i < d.nodeCount(); ++i) {
    const char* name = d.nodeName(i);
    if (name == nullptr) {
      tree.pushBack(Component(true));
      continue;
    }
    auto it = factories_.find(std::string(name));
    if (it == factories_.end()) {
      // Unknown component: placeholder for this subtree, rest still renders.
      // Never dlopen or resolve the name to an address.
      tree.pushBack(Component(true));
      continue;
    }
    StatusOr<Component> made = it->second(d.nodeProps(i));
    if (!made.ok()) return made.status();
    tree.pushBack(made.value());
  }
  return tree;
}

}  // namespace state
}  // namespace muse
