#include "navigation/snapshot.h"

#include <vector>

SnapshotResult captureSnapshot(const NavGraph& graph) noexcept {
  try {
    std::string blob = graph.serialize();
    if (blob.empty() || blob.rfind("NAV1;", 0) != 0) {
      SnapshotResult r;
      r.ok = false;
      r.blob.clear();
      r.error = SnapshotError::CORRUPT;
      return r;
    }
    SnapshotResult r;
    r.ok = true;
    r.blob = blob;
    r.error = SnapshotError::NONE;
    return r;
  } catch (...) {
    SnapshotResult r;
    r.ok = false;
    r.blob.clear();
    r.error = SnapshotError::CORRUPT;
    return r;
  }
}

namespace {

// Collect route ids in first-appearance order from the canonical blob's
// tabs + modals sections. Returns empty on any parse anomaly.
std::vector<std::string> RouteIdsInBlob(const std::string& blob) {
  std::vector<std::string> ids;
  auto add = [&ids](const std::string& route) {
    if (route.empty()) return;
    for (const auto& e : ids) {
      if (e == route) return;
    }
    ids.push_back(route);
  };
  // Blob: NAV1;v=1;sel=<n>;tabs=<a,b|c>;modals=<csv>;drawer=<0|1>
  std::string::size_type tabsPos = blob.find(";tabs=");
  std::string::size_type modalsPos = blob.find(";modals=");
  std::string::size_type drawerPos = blob.find(";drawer=");
  if (tabsPos == std::string::npos || modalsPos == std::string::npos ||
      drawerPos == std::string::npos || !(tabsPos < modalsPos) ||
      !(modalsPos < drawerPos)) {
    return {};
  }
  std::string tabsBody = blob.substr(tabsPos + 6, modalsPos - (tabsPos + 6));
  std::string modalsBody =
      blob.substr(modalsPos + 8, drawerPos - (modalsPos + 8));
  std::string cur;
  auto flush = [&add, &cur]() {
    add(cur);
    cur.clear();
  };
  for (char c : tabsBody) {
    if (c == ',' || c == '|') {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  for (char c : modalsBody) {
    if (c == ',') {
      flush();
    } else {
      cur.push_back(c);
    }
  }
  flush();
  return ids;
}

std::string SafeProvide(const std::function<std::string(const std::string&)>& p,
                        const std::string& route) noexcept {
  if (!p) return "";
  try {
    return p(route);
  } catch (...) {
    return "";
  }
}

}  // namespace

SnapshotResult captureSnapshotWithContent(const NavGraph& graph,
                                          ScrollProvider scroll,
                                          FocusProvider focus) noexcept {
  try {
    std::string blob = graph.serialize();
    if (blob.empty() || blob.rfind("NAV1;", 0) != 0) {
      SnapshotResult r;
      r.ok = false;
      r.blob.clear();
      r.error = SnapshotError::CORRUPT;
      return r;
    }
    std::vector<std::string> ids = RouteIdsInBlob(blob);
    std::string content;
    for (const auto& route : ids) {
      std::string s = SafeProvide(scroll, route);
      std::string f = SafeProvide(focus, route);
      if (s.empty() && f.empty()) continue;
      if (!content.empty()) content.push_back('|');
      content += route + "=scroll:" + s + ",focus:" + f;
    }
    // Fully opted out: keep the bare blob so NavGraph::restore parses it.
    SnapshotResult r;
    r.ok = true;
    r.blob = content.empty() ? blob : blob + ";content=" + content;
    r.error = SnapshotError::NONE;
    return r;
  } catch (...) {
    SnapshotResult r;
    r.ok = false;
    r.blob.clear();
    r.error = SnapshotError::CORRUPT;
    return r;
  }
}
