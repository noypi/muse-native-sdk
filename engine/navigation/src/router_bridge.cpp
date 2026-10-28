#include "navigation/router_bridge.h"
#include "navigation/snapshot.h"

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
  // T1–T3 blob: structural canonical envelope plus opt-in content sections
  // when T2 providers are installed. Never throws: fall back to the bare
  // structural blob on any failure.
  try {
    if (scroll_ || focus_) {
      SnapshotResult cap = captureSnapshotWithContent(graph_, scroll_, focus_);
      if (cap.ok && !cap.blob.empty()) return cap.blob;
    } else {
      SnapshotResult cap = captureSnapshot(graph_);
      if (cap.ok && !cap.blob.empty()) return cap.blob;
    }
  } catch (...) {
  }
  return graph_.serialize();
}

void RouterBridge::setScrollProvider(ScrollProvider provider) {
  scroll_ = std::move(provider);
}

void RouterBridge::setFocusProvider(FocusProvider provider) {
  focus_ = std::move(provider);
}

void RouterBridge::setValidateHook(ValidateFn validate) {
  validate_ = std::move(validate);
}

void RouterBridge::setGuardHook(GuardFn guard) {
  guard_ = std::move(guard);
}

bool RouterBridge::restoreFromLifeSave(const std::string& blob) {
  // T6 kill-restore path: route through the T5 commit gate (validate+guard)
  // with pass-through defaults when no hooks are installed. Returns false
  // only on CORRUPT/VALIDATION_REJECTED/GUARD_DENIED; a home degrade
  // (UNKNOWN_ROUTE_HOME) commits home and returns true. Never throws.
  try {
    RestoreResult gate = commitRestore(blob, validate_, guard_);
    if (!gate.committed) {
      graph_ = NavGraph(routes_);
      return false;
    }
    if (gate.error == RestoreError::UNKNOWN_ROUTE_HOME) {
      graph_ = NavGraph(routes_);
      return true;
    }
    // Committed with content intact: strip the optional `;content=` section
    // before the canonical graph restore (the graph parser owns only the
    // structural prefix). The gate already validated the structural routes.
    std::string structural = blob;
    std::string::size_type cpos = structural.find(";content=");
    if (cpos != std::string::npos) structural.erase(cpos);
    NavResult r = graph_.restore(structural);
    if (!r.ok) {
      // Committed per the gate but the local graph rejected the blob (e.g.
      // route table drift): degrade to home rather than crash. A structurally
      // sound blob means unknown-route degrade (true); anything else is a
      // corrupt restore (false). Mirror decodeState's well-formedness check
      // without reimplementing it: reuse commitRestore's verdict is already
      // consumed, so fall back to a local home reset reporting true only
      // when the blob at least carries the canonical envelope.
      graph_ = NavGraph(routes_);
      return structural.rfind("NAV1;", 0) == 0;
    }
    return true;
  } catch (...) {
    try {
      graph_ = NavGraph(routes_);
    } catch (...) {
    }
    return false;
  }
}
