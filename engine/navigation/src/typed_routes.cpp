#include "navigation/typed_routes.h"

namespace {

const char* typeToken(ParamType t) {
  switch (t) {
    case ParamType::Int:
      return "int";
    case ParamType::Double:
      return "double";
    case ParamType::Bool:
      return "bool";
    case ParamType::String:
      return "string";
  }
  return "string";
}

std::string diagnosticFor(const std::string& routeId, const std::string& param,
                          const std::string& expected,
                          const std::string& actual) {
  return routeId + "." + param + ": expected " + expected + ", got " + actual;
}

}  // namespace

ParamVerdict validateParams(const RouteDescriptor& desc,
                            const std::map<std::string, TypedParam>& args) {
  for (const ParamDecl& decl : desc.params) {
    auto it = args.find(decl.name);
    if (it == args.end()) {
      if (decl.required) {
        ParamVerdict v;
        v.ok = false;
        v.error = ParamError::MISSING_REQUIRED;
        v.diagnostic = diagnosticFor(desc.routeId, decl.name,
                                     typeToken(decl.type), "missing");
        return v;
      }
      continue;
    }
    if (it->second.type != decl.type) {
      ParamVerdict v;
      v.ok = false;
      v.error = ParamError::TYPE_MISMATCH;
      v.diagnostic = diagnosticFor(desc.routeId, decl.name,
                                   typeToken(decl.type),
                                   typeToken(it->second.type));
      return v;
    }
  }
  for (const auto& kv : args) {
    bool declared = false;
    for (const ParamDecl& decl : desc.params) {
      if (decl.name == kv.first) {
        declared = true;
        break;
      }
    }
    if (!declared) {
      ParamVerdict v;
      v.ok = false;
      v.error = ParamError::UNKNOWN_PARAM;
      v.diagnostic = diagnosticFor(desc.routeId, kv.first, "none",
                                   typeToken(kv.second.type));
      return v;
    }
  }
  ParamVerdict v;
  v.ok = true;
  v.error = ParamError::NONE;
  return v;
}
