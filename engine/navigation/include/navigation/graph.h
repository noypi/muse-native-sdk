#pragma once

#include <string>
#include <vector>

enum class NavError {
  NONE,
  UNKNOWN_ROUTE,
  SCHEMA_MISMATCH,
  GUARD_DENIED,
};

struct NavResult {
  bool ok = false;
  NavError error = NavError::NONE;
};

enum class NavOpType {
  Push,
  Pop,
  Replace,
  GoTo,
  SwitchTab,
  PushModal,
  DismissModal,
  OpenDrawer,
  CloseDrawer,
};

struct NavOp {
  NavOpType type = NavOpType::Push;
  std::string routeId;
  int tabIndex = 0;
  // GoTo reuses tabIndex as the signed delta (e.g. -1 = one step back),
  // per the NAV-006 tester contract (nav_006_regress_test.cpp).
};

// Unified navigation graph owning the main tab stacks, modal stack and
// drawer flag. Every mutation goes through apply(); serialize() emits the
// one canonical format (also consumed by T6 state_schema).
class NavGraph {
 public:
  explicit NavGraph(std::vector<std::string> routes);

  NavResult apply(NavOp op);
  std::string serialize() const;
  NavResult restore(const std::string& blob);

 private:
  bool isKnownRoute(const std::string& route) const;

  std::vector<std::string> routes_;
  std::string home_;
  std::vector<std::vector<std::string>> tabStacks_;
  int selectedTab_ = 0;
  std::vector<std::string> modalStack_;
  bool drawerOpen_ = false;
};
