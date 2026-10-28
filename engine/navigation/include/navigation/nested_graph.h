#pragma once

#include <string>
#include <vector>

#include "navigation/graph.h"

struct GraphDecl {
  std::string graphId;
  std::string parent;
  std::vector<std::string> routes;
};

struct NestedManifest {
  std::vector<GraphDecl> graphs;
};

struct NestedResult {
  bool ok = false;
  std::vector<std::string> stack;
  NavError error = NavError::NONE;
};

// Nested navigator deeplink resolution (FR-NAV-007 / AC-NAV-018). Each child
// graph owns an independent stack; resolving a nested leaf materializes the
// full parent chain as one transaction. Unknown graph or unknown nested route
// degrades to the home stack.
NestedResult resolveNested(const NestedManifest& m, const std::string& host,
                           const std::vector<std::string>& path);
