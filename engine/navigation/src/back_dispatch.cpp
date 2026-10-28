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
    // Deterministic tie-break: higher priority wins; equal priorities go
    // to the lexicographically smallest graphId.
    if (entry.second > best->second ||
        (entry.second == best->second && entry.first < best->first)) {
      best = &entry;
    }
  }
  return best->first;
}
