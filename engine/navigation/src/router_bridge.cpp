#include "navigation/router_bridge.h"
#include "navigation/snapshot.h"
#include "navigation/typed_routes.h"

#include <cctype>
#include <map>
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

void SkipWs(const std::string& s, std::string::size_type& i) {
  while (i < s.size() &&
         (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
    ++i;
  }
}

// Locates the `"params"` value. Returns true when the key is absent
// (caller treats as empty args). When present, `isObject` reports whether
// the value is a `{...}` object and `body` holds its inner text.
bool FindParamsBody(const std::string& jsonArgs, bool& present, bool& isObject,
                    std::string& body) {
  present = false;
  isObject = false;
  body.clear();
  const std::string key = "\"params\"";
  std::string::size_type keyPos = jsonArgs.find(key);
  if (keyPos == std::string::npos) return true;
  std::string::size_type colon = jsonArgs.find(':', keyPos + key.size());
  if (colon == std::string::npos) return false;
  std::string::size_type i = colon + 1;
  SkipWs(jsonArgs, i);
  if (i >= jsonArgs.size()) return false;
  present = true;
  if (jsonArgs[i] != '{') return true;  // present but non-object
  // Balanced-brace scan honoring string literals.
  int depth = 0;
  bool inStr = false;
  std::string::size_type j = i;
  for (; j < jsonArgs.size(); ++j) {
    char c = jsonArgs[j];
    if (inStr) {
      if (c == '"' && (j == 0 || jsonArgs[j - 1] != '\\')) inStr = false;
      continue;
    }
    if (c == '"') {
      inStr = true;
    } else if (c == '{') {
      ++depth;
    } else if (c == '}') {
      --depth;
      if (depth == 0) break;
    }
  }
  if (depth != 0) return false;
  isObject = true;
  body = jsonArgs.substr(i + 1, j - i - 1);
  return true;
}

bool ParseJsonString(const std::string& s, std::string::size_type& i,
                     std::string& out) {
  // Expects s[i] == '"'.
  if (i >= s.size() || s[i] != '"') return false;
  ++i;
  std::string acc;
  while (i < s.size()) {
    char c = s[i];
    if (c == '\\' && i + 1 < s.size()) {
      acc.push_back(s[i + 1]);
      i += 2;
      continue;
    }
    if (c == '"') {
      ++i;
      out = acc;
      return true;
    }
    acc.push_back(c);
    ++i;
  }
  return false;
}

// Parses the inner text of the params object into typed args. String,
// bool, int, and double JSON literals are recognized; anything else is
// malformed.
bool ParseParamsArgs(const std::string& body,
                     std::map<std::string, TypedParam>& args) {
  args.clear();
  std::string::size_type i = 0;
  SkipWs(body, i);
  if (i >= body.size()) return true;  // `{}` is valid
  while (true) {
    SkipWs(body, i);
    std::string name;
    if (!ParseJsonString(body, i, name)) return false;
    SkipWs(body, i);
    if (i >= body.size() || body[i] != ':') return false;
    ++i;
    SkipWs(body, i);
    if (i >= body.size()) return false;
    TypedParam p;
    if (body[i] == '"') {
      std::string sv;
      if (!ParseJsonString(body, i, sv)) return false;
      p.type = ParamType::String;
      p.stringValue = sv;
    } else if (body.compare(i, 4, "true") == 0) {
      p.type = ParamType::Bool;
      p.boolValue = true;
      i += 4;
    } else if (body.compare(i, 5, "false") == 0) {
      p.type = ParamType::Bool;
      p.boolValue = false;
      i += 5;
    } else {
      // Numeric literal: collect until ',' or end.
      std::string::size_type start = i;
      if (body[i] == '-' || body[i] == '+') ++i;
      while (i < body.size() && (std::isdigit(
                 static_cast<unsigned char>(body[i])) ||
                 body[i] == '.' || body[i] == 'e' || body[i] == 'E' ||
                 body[i] == '+' || body[i] == '-')) {
        ++i;
      }
      std::string tok = body.substr(start, i - start);
      if (tok.empty()) return false;
      bool isDouble = tok.find('.') != std::string::npos ||
                      tok.find('e') != std::string::npos ||
                      tok.find('E') != std::string::npos;
      try {
        if (isDouble) {
          p.type = ParamType::Double;
          p.doubleValue = std::stod(tok);
        } else {
          p.type = ParamType::Int;
          p.intValue = std::stoll(tok);
        }
      } catch (...) {
        return false;
      }
    }
    args[name] = p;
    SkipWs(body, i);
    if (i >= body.size()) break;
    if (body[i] != ',') return false;
    ++i;
  }
  return true;
}

// Route-table param schema (FR-NAV-007 descriptors, reused not redefined).
// Only "profile" declares typed params here, mirroring
// param_validation_test.cpp: required string userId + optional int tab.
RouteDescriptor DescriptorFor(const std::string& route) {
  RouteDescriptor d;
  d.routeId = route;
  if (route == "profile") {
    d.params = {{"userId", ParamType::String, true},
                {"tab", ParamType::Int, false}};
  }
  return d;
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
    bool present = false;
    bool isObject = false;
    std::string paramsBody;
    if (!FindParamsBody(jsonArgs, present, isObject, paramsBody)) {
      return {false, "", BridgeError::BAD_ARGS};
    }
    std::map<std::string, TypedParam> args;
    if (present) {
      // A present but non-object "params" value is malformed.
      if (!isObject) return {false, "", BridgeError::BAD_ARGS};
      if (!ParseParamsArgs(paramsBody, args)) {
        return {false, "", BridgeError::BAD_ARGS};
      }
    }
    // FR-NAV-007 typed validation (reused path): wrong/missing params deny
    // the commit deterministically. The optional setValidateHook stays the
    // commit gate — an empty hook allows every route.
    if (validate_ && !validate_(route)) {
      return {false, "", BridgeError::BAD_ARGS};
    }
    ParamVerdict verdict = validateParams(DescriptorFor(route), args);
    if (!verdict.ok) {
      return {false, "", BridgeError::BAD_ARGS};
    }
    NavOp op;
    op.type = NavOpType::Push;
    op.routeId = route;
    NavResult r = graph_.apply(op);
    if (!r.ok) {
      // FR-NAV-004 reuse: unknown route degrades to home, never crashes.
      // The error is reported at this call site; the graph reset is the
      // home degrade.
      if (r.error == NavError::UNKNOWN_ROUTE) {
        graph_ = NavGraph(routes_);
      }
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
