#include "navigation/trie.h"

RouteTrie::RouteTrie(std::vector<RouteEntry> routes)
    : routes_(std::move(routes)) {}

namespace {

bool IsCapture(const std::string& segment) {
  return segment.size() > 1 && segment[0] == ':';
}

bool CoerceInt(const std::string& raw, long long& out) {
  if (raw.empty()) return false;
  size_t pos = 0;
  try {
    long long v = std::stoll(raw, &pos);
    if (pos != raw.size()) return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool CoerceDouble(const std::string& raw, double& out) {
  if (raw.empty()) return false;
  size_t pos = 0;
  try {
    double v = std::stod(raw, &pos);
    if (pos != raw.size()) return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

bool CoerceBool(const std::string& raw, bool& out) {
  if (raw == "true" || raw == "1") {
    out = true;
    return true;
  }
  if (raw == "false" || raw == "0") {
    out = false;
    return true;
  }
  return false;
}

bool CoerceQueryParam(ParamType type, const std::string& raw,
                      TypedParam& out) {
  TypedParam p;
  p.type = type;
  switch (type) {
    case ParamType::Int:
      if (!CoerceInt(raw, p.intValue)) return false;
      break;
    case ParamType::Double:
      if (!CoerceDouble(raw, p.doubleValue)) return false;
      break;
    case ParamType::Bool:
      if (!CoerceBool(raw, p.boolValue)) return false;
      break;
    case ParamType::String:
      p.stringValue = raw;
      break;
  }
  out = p;
  return true;
}

}  // namespace

MatchResult RouteTrie::match(
    const std::string& host, const std::vector<std::string>& path,
    const std::map<std::string, std::string>& query) const {
  for (const RouteEntry& route : routes_) {
    if (route.host != host) continue;
    if (route.path.size() != path.size()) continue;
    std::map<std::string, TypedParam> args;
    bool pathOk = true;
    for (size_t i = 0; i < route.path.size(); ++i) {
      const std::string& pattern = route.path[i];
      if (IsCapture(pattern)) {
        TypedParam p;
        p.type = ParamType::String;
        p.stringValue = path[i];
        args.emplace(pattern.substr(1), p);
      } else if (pattern != path[i]) {
        pathOk = false;
        break;
      }
    }
    if (!pathOk) continue;

    // Deterministic query diagnostics: required params first (missing
    // before type errors on other keys would be order-dependent, so walk
    // the manifest in declaration order and report the first failure).
    bool mismatch = false;
    bool missing = false;
    for (const QueryParam& qp : route.query) {
      auto it = query.find(qp.name);
      if (it == query.end()) {
        if (qp.required) {
          missing = true;
          break;
        }
        continue;
      }
      TypedParam coerced;
      if (!CoerceQueryParam(qp.type, it->second, coerced)) {
        mismatch = true;
        break;
      }
      args.emplace(qp.name, coerced);
    }
    if (missing) {
      return MatchResult{false, "", {}, MatchError::MISSING_PARAM};
    }
    if (mismatch) {
      return MatchResult{false, "", {}, MatchError::TYPE_MISMATCH};
    }
    MatchResult ok;
    ok.matched = true;
    ok.routeId = route.routeId;
    ok.args = args;
    ok.error = MatchError::NONE;
    return ok;
  }
  return MatchResult{false, "", {}, MatchError::NO_MATCH};
}
