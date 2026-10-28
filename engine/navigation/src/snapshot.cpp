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

namespace {

// Split helper for truncation (',' or '|' aware callers split manually).
std::vector<std::string> SplitOn(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

std::string JoinWith(const std::vector<std::string>& items, char delim) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) out.push_back(delim);
    out += items[i];
  }
  return out;
}

void Emit(TelemetrySink& sink, const std::string& detail) noexcept {
  if (!sink) return;
  try {
    sink("nav.snapshot.truncated", detail);
  } catch (...) {
  }
}

// Parsed structural blob: NAV1;v=1;sel=<n>;tabs=...;modals=...;drawer=0|1
struct Structural {
  bool valid = false;
  int sel = 0;
  std::vector<std::vector<std::string>> tabs;
  std::vector<std::string> modals;
  bool drawer = false;
};

Structural ParseStructural(const std::string& structural) {
  Structural st;
  if (structural.rfind("NAV1;", 0) != 0) return st;
  std::string body = structural.substr(5);
  std::vector<std::string> fields = SplitOn(body, ';');
  if (fields.size() != 5) return st;
  if (fields[0] != "v=1") return st;
  if (fields[1].rfind("sel=", 0) != 0) return st;
  if (fields[2].rfind("tabs=", 0) != 0) return st;
  if (fields[3].rfind("modals=", 0) != 0) return st;
  if (fields[4].rfind("drawer=", 0) != 0) return st;
  int sel = 0;
  const std::string selBody = fields[1].substr(4);
  if (selBody.empty()) return st;
  for (char c : selBody) {
    if (c < '0' || c > '9') return st;
    sel = sel * 10 + (c - '0');
  }
  const std::string tabsBody = fields[2].substr(5);
  const std::string modalsBody = fields[3].substr(7);
  const std::string drawerBody = fields[4].substr(7);
  if (drawerBody != "0" && drawerBody != "1") return st;
  std::vector<std::vector<std::string>> tabs;
  for (const std::string& part : SplitOn(tabsBody, '|')) {
    if (part.empty()) return st;
    std::vector<std::string> stack = SplitOn(part, ',');
    if (stack.empty()) return st;
    for (const auto& r : stack) {
      if (r.empty()) return st;
    }
    tabs.push_back(stack);
  }
  if (tabs.empty()) return st;
  if (sel < 0 || static_cast<std::size_t>(sel) >= tabs.size()) return st;
  std::vector<std::string> modals;
  if (!modalsBody.empty()) modals = SplitOn(modalsBody, ',');
  st.valid = true;
  st.sel = sel;
  st.tabs = std::move(tabs);
  st.modals = std::move(modals);
  st.drawer = (drawerBody == "1");
  return st;
}

std::string SerializeStructural(const Structural& st) {
  std::string tabs;
  for (std::size_t i = 0; i < st.tabs.size(); ++i) {
    if (i > 0) tabs.push_back('|');
    tabs += JoinWith(st.tabs[i], ',');
  }
  return "NAV1;v=1;sel=" + std::to_string(st.sel) + ";tabs=" + tabs +
         ";modals=" + JoinWith(st.modals, ',') + ";drawer=" +
         (st.drawer ? "1" : "0");
}

// Content entry: route=scroll:<s>,focus:<f>
struct ContentEntry {
  std::string route;
  std::string scroll;
  std::string focus;
};

std::vector<ContentEntry> ParseContent(const std::string& body) {
  std::vector<ContentEntry> entries;
  if (body.empty()) return entries;
  for (const std::string& part : SplitOn(body, '|')) {
    std::string::size_type eq = part.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    ContentEntry e;
    e.route = part.substr(0, eq);
    std::string rest = part.substr(eq + 1);
    const std::string kScroll = "scroll:";
    const std::string kFocusSep = ",focus:";
    if (rest.rfind(kScroll, 0) != 0) continue;
    std::string::size_type fpos = rest.find(kFocusSep);
    if (fpos == std::string::npos) continue;
    e.scroll = rest.substr(kScroll.size(), fpos - kScroll.size());
    e.focus = rest.substr(fpos + kFocusSep.size());
    entries.push_back(e);
  }
  return entries;
}

std::string SerializeContent(const std::vector<ContentEntry>& entries) {
  std::string out;
  for (const auto& e : entries) {
    if (!out.empty()) out.push_back('|');
    out += e.route + "=scroll:" + e.scroll + ",focus:" + e.focus;
  }
  return out;
}

}  // namespace

SnapshotResult truncateSnapshot(const std::string& blob, size_t maxBytes,
                                TelemetrySink sink) noexcept {
  try {
    SnapshotResult corrupt;
    corrupt.ok = false;
    corrupt.blob.clear();
    corrupt.error = SnapshotError::CORRUPT;
    corrupt.truncated = false;
    if (blob.empty()) return corrupt;
    if (blob.size() <= maxBytes) {
      SnapshotResult r;
      r.ok = true;
      r.blob = blob;
      r.error = SnapshotError::NONE;
      r.truncated = false;
      return r;
    }
    if (blob.rfind("NAV1;", 0) != 0) return corrupt;
    // Split structural prefix (5 ';'-fields) from optional ;content=.
    std::string structural = blob;
    std::string contentBody;
    std::string::size_type cpos = blob.find(";content=");
    if (cpos != std::string::npos) {
      structural = blob.substr(0, cpos);
      contentBody = blob.substr(cpos + 9);
    }
    Structural st = ParseStructural(structural);
    if (!st.valid) return corrupt;
    std::vector<ContentEntry> entries = ParseContent(contentBody);
    bool hasContent = (cpos != std::string::npos);

    auto rebuild = [&](const Structural& s,
                       const std::vector<ContentEntry>& e, bool withContent) {
      std::string base = SerializeStructural(s);
      if (withContent && !e.empty()) return base + ";content=" + SerializeContent(e);
      return base;
    };

    // Priority 1: drop focus values (keep scroll).
    if (hasContent && !entries.empty()) {
      bool anyFocus = false;
      for (const auto& e : entries) {
        if (!e.focus.empty()) {
          anyFocus = true;
          break;
        }
      }
      if (anyFocus) {
        std::vector<ContentEntry> noFocus = entries;
        for (auto& e : noFocus) e.focus.clear();
        std::string candidate = rebuild(st, noFocus, true);
        Emit(sink, "focus");
        if (candidate.size() <= maxBytes) {
          SnapshotResult r;
          r.ok = true;
          r.blob = candidate;
          r.error = SnapshotError::NONE;
          r.truncated = true;
          return r;
        }
        entries = noFocus;
        hasContent = true;
      }
      // Priority 2: drop scroll entirely (remove ;content= section).
      std::string bare = SerializeStructural(st);
      Emit(sink, "scroll");
      if (bare.size() <= maxBytes) {
        SnapshotResult r;
        r.ok = true;
        r.blob = bare;
        r.error = SnapshotError::NONE;
        r.truncated = true;
        return r;
      }
      structural = bare;
    }

    // Priority 3: drop deepest stack entries; home root (index 0 of each
    // tab) is never removed; modals drain fully first (top = deepest).
    Structural cur = st;
    // If content was absent from the start, structural already parsed.
    if (cpos == std::string::npos) structural = SerializeStructural(cur);
    while (true) {
      // Find deepest droppable: longest tab stack with size > 1, else modal.
      std::size_t bestTab = cur.tabs.size();
      std::size_t bestDepth = 1;
      for (std::size_t i = 0; i < cur.tabs.size(); ++i) {
        if (cur.tabs[i].size() > bestDepth) {
          bestDepth = cur.tabs[i].size();
          bestTab = i;
        }
      }
      bool canTab = (bestTab < cur.tabs.size());
      bool canModal = !cur.modals.empty();
      // Prefer modals (topmost overlay = deepest) on ties.
      if (canModal && (!canTab || cur.modals.size() >= bestDepth)) {
        std::string dropped = cur.modals.back();
        cur.modals.pop_back();
        Emit(sink, "modal:" + dropped);
      } else if (canTab) {
        std::string dropped = cur.tabs[bestTab].back();
        cur.tabs[bestTab].pop_back();
        Emit(sink, "stack:" + dropped);
      } else {
        break;  // only home roots remain
      }
      std::string candidate = SerializeStructural(cur);
      if (candidate.size() <= maxBytes) {
        SnapshotResult r;
        r.ok = true;
        r.blob = candidate;
        r.error = SnapshotError::NONE;
        r.truncated = true;
        return r;
      }
    }
    // Even the home root exceeds budget.
    return corrupt;
  } catch (...) {
    SnapshotResult r;
    r.ok = false;
    r.blob.clear();
    r.error = SnapshotError::CORRUPT;
    r.truncated = false;
    return r;
  }
}
