#include "navigation/back_dispatch.h"

#include <string>
#include <unordered_map>

namespace {

std::unordered_map<std::string, int>& Registry() {
  static std::unordered_map<std::string, int> registry;
  return registry;
}

}  // namespace

void registerGraph(const std::string& graphId, int priority) {
  Registry()[graphId] = priority;
}

void unregisterGraph(const std::string& graphId) {
  Registry().erase(graphId);
}

std::string dispatchBack() {
  const auto& registry = Registry();
  if (registry.empty()) {
    return "home";
  }
  const auto* best = &*registry.begin();
  for (const auto& entry : registry) {
    if (entry.second > best->second) {
      best = &entry;
    }
  }
  return best->first;
}
