#pragma once

#include <map>
#include <string>

#include "navigation/trie.h"

enum class GuardError {
  NONE,
  AUTH_REQUIRED,
  FLAG_OFF,
  ENTITLEMENT_DENIED,
};

struct GuardRequest {
  std::string routeId;
  std::map<std::string, TypedParam> args;
  bool authenticated = false;
  std::map<std::string, bool> flags;
};

struct GuardVerdict {
  bool allowed = false;
  std::string redirect;
  GuardError error = GuardError::ENTITLEMENT_DENIED;
};

// Route guards (FR-NAV-003 / AC-NAV-006). evaluateGuard checks the
// per-route rule table before any NavGraph::apply commit; a denied commit
// returns a redirect route and never mutates the stack. reRunOnRestore
// re-evaluates the same rules against a restored request and returns the
// verdict (true = restore allowed, false = restore denied); it mirrors
// evaluateGuard(req).allowed.
GuardVerdict evaluateGuard(const GuardRequest& req);
bool reRunOnRestore(const GuardRequest& req);
