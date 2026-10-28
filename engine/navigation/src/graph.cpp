#include "navigation/graph.h"

#include <sstream>
#include <utility>

namespace {

constexpr const char* kPrefix = "NAV1;";
constexpr const char* kVersionToken = "v=1;";

std::vector<std::string> Split(const std::string& s, char delim) {
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

bool ParseInt(const std::string& s, int& out) {
  if (s.empty()) return false;
  int value = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + (c - '0');
  }
  out = value;
  return true;
}

std::string Join(const std::vector<std::string>& items, char delim) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i > 0) out.push_back(delim);
    out += items[i];
  }
  return out;
}

}  // namespace

NavGraph::NavGraph(std::vector<std::string> routes)
    : routes_(std::move(routes)) {
  home_ = routes_.empty() ? "" : routes_.front();
  // Two tabs by default, each rooted at home; grows on SwitchTab.
  tabStacks_.assign(2, std::vector<std::string>{home_});
}

bool NavGraph::isKnownRoute(const std::string& route) const {
  for (const auto& r : routes_) {
    if (r == route) return true;
  }
  return false;
}

NavResult NavGraph::apply(NavOp op) {
  switch (op.type) {
    case NavOpType::Push: {
      if (!isKnownRoute(op.routeId)) return {false, NavError::UNKNOWN_ROUTE};
      tabStacks_[static_cast<std::size_t>(selectedTab_)].push_back(op.routeId);
      return {true, NavError::NONE};
    }
    case NavOpType::Pop: {
      auto& stack = tabStacks_[static_cast<std::size_t>(selectedTab_)];
      if (stack.size() > 1) stack.pop_back();
      return {true, NavError::NONE};
    }
    case NavOpType::SwitchTab: {
      if (op.tabIndex < 0) return {false, NavError::SCHEMA_MISMATCH};
      if (static_cast<std::size_t>(op.tabIndex) >= tabStacks_.size()) {
        tabStacks_.resize(static_cast<std::size_t>(op.tabIndex) + 1,
                          std::vector<std::string>{home_});
      }
      selectedTab_ = op.tabIndex;
      return {true, NavError::NONE};
    }
    case NavOpType::PushModal: {
      if (!isKnownRoute(op.routeId)) return {false, NavError::UNKNOWN_ROUTE};
      modalStack_.push_back(op.routeId);
      return {true, NavError::NONE};
    }
    case NavOpType::DismissModal: {
      if (!modalStack_.empty()) modalStack_.pop_back();
      return {true, NavError::NONE};
    }
    case NavOpType::OpenDrawer: {
      drawerOpen_ = true;
      return {true, NavError::NONE};
    }
    case NavOpType::CloseDrawer: {
      drawerOpen_ = false;
      return {true, NavError::NONE};
    }
  }
  return {false, NavError::SCHEMA_MISMATCH};
}

std::string NavGraph::serialize() const {
  std::string tabs;
  for (std::size_t i = 0; i < tabStacks_.size(); ++i) {
    if (i > 0) tabs.push_back('|');
    tabs += Join(tabStacks_[i], ',');
  }
  std::ostringstream out;
  out << kPrefix << kVersionToken << "sel=" << selectedTab_ << ";tabs=" << tabs
      << ";modals=" << Join(modalStack_, ',') << ";drawer="
      << (drawerOpen_ ? "1" : "0");
  return out.str();
}

NavResult NavGraph::restore(const std::string& blob) {
  if (blob.rfind(kPrefix, 0) != 0) return {false, NavError::SCHEMA_MISMATCH};
  std::string body = blob.substr(5);
  // Expected: v=1;sel=<n>;tabs=<a,b|c>;modals=<csv>;drawer=<0|1>
  std::vector<std::string> fields = Split(body, ';');
  if (fields.size() != 5) return {false, NavError::SCHEMA_MISMATCH};
  if (fields[0] != "v=1") return {false, NavError::SCHEMA_MISMATCH};
  if (fields[1].rfind("sel=", 0) != 0) return {false, NavError::SCHEMA_MISMATCH};
  if (fields[2].rfind("tabs=", 0) != 0) return {false, NavError::SCHEMA_MISMATCH};
  if (fields[3].rfind("modals=", 0) != 0)
    return {false, NavError::SCHEMA_MISMATCH};
  if (fields[4].rfind("drawer=", 0) != 0)
    return {false, NavError::SCHEMA_MISMATCH};

  int sel = 0;
  if (!ParseInt(fields[1].substr(4), sel)) return {false, NavError::SCHEMA_MISMATCH};

  std::string tabsBody = fields[2].substr(5);
  std::string modalsBody = fields[3].substr(7);
  std::string drawerBody = fields[4].substr(7);
  if (drawerBody != "0" && drawerBody != "1")
    return {false, NavError::SCHEMA_MISMATCH};

  std::vector<std::vector<std::string>> tabs;
  for (const std::string& part : Split(tabsBody, '|')) {
    std::vector<std::string> stack;
    if (!part.empty()) stack = Split(part, ',');
    if (stack.empty()) return {false, NavError::SCHEMA_MISMATCH};
    for (const auto& route : stack) {
      if (!isKnownRoute(route)) return {false, NavError::SCHEMA_MISMATCH};
    }
    tabs.push_back(std::move(stack));
  }
  if (tabs.empty()) return {false, NavError::SCHEMA_MISMATCH};
  if (sel < 0 || static_cast<std::size_t>(sel) >= tabs.size())
    return {false, NavError::SCHEMA_MISMATCH};

  std::vector<std::string> modals;
  if (!modalsBody.empty()) {
    modals = Split(modalsBody, ',');
    for (const auto& route : modals) {
      if (!isKnownRoute(route)) return {false, NavError::SCHEMA_MISMATCH};
    }
  }

  tabStacks_ = std::move(tabs);
  selectedTab_ = sel;
  modalStack_ = std::move(modals);
  drawerOpen_ = (drawerBody == "1");
  return {true, NavError::NONE};
}
