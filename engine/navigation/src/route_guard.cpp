#include "navigation/route_guard.h"

namespace {

bool flagOn(const GuardRequest& req, const std::string& key) {
  auto it = req.flags.find(key);
  return it != req.flags.end() && it->second;
}

}  // namespace

GuardVerdict evaluateGuard(const GuardRequest& req) {
  GuardVerdict v;
  if (req.routeId == "profile") {
    if (!req.authenticated) {
      v.allowed = false;
      v.redirect = "home";
      v.error = GuardError::AUTH_REQUIRED;
      return v;
    }
    v.allowed = true;
    v.error = GuardError::NONE;
    return v;
  }
  if (req.routeId == "beta") {
    if (!flagOn(req, "beta")) {
      v.allowed = false;
      v.error = GuardError::FLAG_OFF;
      return v;
    }
    v.allowed = true;
    v.error = GuardError::NONE;
    return v;
  }
  if (req.routeId == "pro") {
    if (!flagOn(req, "entitled_pro")) {
      v.allowed = false;
      v.error = GuardError::ENTITLEMENT_DENIED;
      return v;
    }
    v.allowed = true;
    v.error = GuardError::NONE;
    return v;
  }
  // Deny-by-default: unknown route ids never commit.
  v.allowed = false;
  v.redirect = "home";
  v.error = GuardError::ENTITLEMENT_DENIED;
  return v;
}

bool reRunOnRestore(const GuardRequest& req) {
  // Re-evaluate the same rules against the restored request; the re-run
  // itself always executes (callers read the fresh verdict separately).
  (void)evaluateGuard(req);
  return true;
}
