#pragma once

#include <map>
#include <string>
#include <vector>

#include "navigation/trie.h"

enum class ParamError {
  NONE,
  MISSING_REQUIRED,
  TYPE_MISMATCH,
  UNKNOWN_PARAM,
};

struct ParamDecl {
  std::string name;
  ParamType type = ParamType::String;
  bool required = false;
};

struct RouteDescriptor {
  std::string routeId;
  std::vector<ParamDecl> params;
};

struct ParamVerdict {
  bool ok = false;
  ParamError error = ParamError::NONE;
  std::string diagnostic;
};

// Typed param boundary validation (FR-NAV-007 / AC-NAV-017). Validates the
// args map against the route descriptor before commit; every declared-param
// failure yields a stable diagnostic:
//   `routeId.param: expected <type>, got <actual|missing>`
// with lowercase type tokens (int, double, bool, string). Unknown params
// are rejected with UNKNOWN_PARAM.
ParamVerdict validateParams(const RouteDescriptor& desc,
                            const std::map<std::string, TypedParam>& args);
