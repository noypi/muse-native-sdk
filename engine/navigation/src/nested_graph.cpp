#include "navigation/nested_graph.h"

namespace {

const GraphDecl* findGraph(const NestedManifest& m, const std::string& id) {
  for (const GraphDecl& g : m.graphs) {
    if (g.graphId == id) return &g;
  }
  return nullptr;
}

std::string homeRoute(const NestedManifest& m, const GraphDecl* target) {
  // Walk to the rootmost ancestor; its first route is the home fallback.
  const GraphDecl* cur = target;
  const GraphDecl* root = target;
  while (cur != nullptr && !cur->parent.empty()) {
    const GraphDecl* p = findGraph(m, cur->parent);
    if (p == nullptr) break;
    root = p;
    cur = p;
  }
  if (root != nullptr && !root->routes.empty()) return root->routes[0];
  return "home";
}

NestedResult degrade(const NestedManifest& m, const GraphDecl* target) {
  NestedResult r;
  r.ok = false;
  r.error = NavError::UNKNOWN_ROUTE;
  if (target != nullptr) {
    r.stack = {homeRoute(m, target)};
  } else if (!m.graphs.empty() && !m.graphs[0].routes.empty()) {
    r.stack = {m.graphs[0].routes[0]};
  } else {
    r.stack = {"home"};
  }
  return r;
}

}  // namespace

NestedResult resolveNested(const NestedManifest& m, const std::string& host,
                           const std::vector<std::string>& path) {
  // Host check: the deeplink host must be the canonical app host ("app") or
  // a declared graphId in the manifest. Any other host degrades to home.
  bool hostOk = (host == "app");
  if (!hostOk) {
    for (const GraphDecl& g : m.graphs) {
      if (g.graphId == host) {
        hostOk = true;
        break;
      }
    }
  }
  if (!hostOk) {
    const GraphDecl* any = m.graphs.empty() ? nullptr : &m.graphs[0];
    return degrade(m, any);
  }
  if (path.size() < 2) {
    const GraphDecl* any = m.graphs.empty() ? nullptr : &m.graphs[0];
    return degrade(m, any);
  }
  const std::string& graphId = path[0];
  const std::string& leaf = path[1];
  const GraphDecl* target = findGraph(m, graphId);
  if (target == nullptr) return degrade(m, nullptr);

  // Leaf must be a declared route of the target graph.
  std::size_t leafIdx = target->routes.size();
  for (std::size_t i = 0; i < target->routes.size(); ++i) {
    if (target->routes[i] == leaf) {
      leafIdx = i;
      break;
    }
  }
  if (leafIdx >= target->routes.size()) return degrade(m, target);

  // Build the ancestor chain from the root down to the target's parent.
  std::vector<const GraphDecl*> chain;
  const GraphDecl* cur = target;
  while (cur != nullptr && !cur->parent.empty()) {
    const GraphDecl* p = findGraph(m, cur->parent);
    if (p == nullptr) break;
    chain.insert(chain.begin(), p);
    cur = p;
  }

  NestedResult r;
  r.ok = true;
  r.error = NavError::NONE;
  // Root home first (first route of the rootmost graph in the chain, or of
  // the target itself when it has no parent).
  if (!chain.empty() && !chain.front()->routes.empty()) {
    r.stack.push_back(chain.front()->routes[0]);
  } else if (chain.empty()) {
    // Target is itself a root: home is its first route unless the leaf is
    // that same route (avoid duplicating "home").
    if (!target->routes.empty() && target->routes[0] != leaf) {
      r.stack.push_back(target->routes[0]);
    }
  }
  // Intermediate graphs contribute their full route lists.
  for (std::size_t i = 1; i < chain.size(); ++i) {
    for (const std::string& route : chain[i]->routes) r.stack.push_back(route);
  }
  // Target contributes its prefix up to and including the leaf.
  for (std::size_t i = 0; i <= leafIdx; ++i) {
    r.stack.push_back(target->routes[i]);
  }
  return r;
}
