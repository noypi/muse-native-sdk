#include "navigation/route_transition.h"

#include <map>

namespace {

TransitionSpec kDefaults() { return TransitionSpec(); }

bool validSpec(const TransitionSpec& s) {
  return s.enterMs > 0 && s.exitMs > 0 && s.popMs > 0;
}

std::map<std::string, TransitionSpec>& table() {
  static std::map<std::string, TransitionSpec> t;
  return t;
}

}  // namespace

TransitionSpec transitionFor(const std::string& routeId) {
  auto& t = table();
  auto it = t.find(routeId);
  if (it == t.end()) return kDefaults();
  if (!validSpec(it->second)) return kDefaults();
  return it->second;
}

void setTransition(const std::string& routeId, const TransitionSpec& s) {
  if (!validSpec(s)) {
    table()[routeId] = kDefaults();
    return;
  }
  table()[routeId] = s;
}
