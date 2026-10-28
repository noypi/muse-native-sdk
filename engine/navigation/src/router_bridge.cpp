#include "navigation/router_bridge.h"

#include <utility>

namespace {

// Extracts the "route" string field from a minimal `{"route":"<id>"}` JSON
// object. Returns false when the field is absent or malformed.
bool ParseRouteArg(const std::string& jsonArgs, std::string& out) {
  const std::string key = "\"route\"";
  std::string::size_type keyPos = jsonArgs.find(key);
  if (keyPos == std::string::npos) return false;
  std::string::size_type colon = jsonArgs.find(':', keyPos + key.size());
  if (colon == std::string::npos) return false;
  std::string::size_type open = jsonArgs.find('"', colon + 1);
  if (open == std::string::npos) return false;
  std::string::size_type close = jsonArgs.find('"', open + 1);
  if (close == std::string::npos) return false;
  out = jsonArgs.substr(open + 1, close - open - 1);
  return !out.empty();
}

}  // namespace

RouterBridge::RouterBridge(std::vector<std::string> routes)
    : routes_(routes), graph_(routes) {}

BridgeResult RouterBridge::invoke(const std::string& method,
                                  const std::string& jsonArgs) {
  if (method == "navigate" || method == "push") {
    std::string route;
    if (!ParseRouteArg(jsonArgs, route)) {
      return {false, "", BridgeError::BAD_ARGS};
    }
    NavOp op;
    op.type = NavOpType::Push;
    op.routeId = route;
    NavResult r = graph_.apply(op);
    if (!r.ok) {
      return {false, "", BridgeError::NAV_ERROR};
    }
    return {true, "{\"route\":\"" + route + "\"}", BridgeError::NONE};
  }
  if (method == "pop" || method == "back") {
    NavOp op;
    op.type = NavOpType::Pop;
    graph_.apply(op);
    return {true, "{}", BridgeError::NONE};
  }
  if (method == "snapshot") {
    return {true, snapshotForPersist(), BridgeError::NONE};
  }
  return {false, "", BridgeError::UNKNOWN_METHOD};
}

std::string RouterBridge::snapshotForPersist() const {
  return graph_.serialize();
}

bool RouterBridge::restoreFromLifeSave(const std::string& blob) {
  NavResult r = graph_.restore(blob);
  if (r.ok) return true;
  graph_ = NavGraph(routes_);
  return false;
}
