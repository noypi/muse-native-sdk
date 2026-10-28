#pragma once

#include <functional>
#include <string>

#include "navigation/state_schema.h"

enum class ColdLinkError {
  NONE,
  NO_MATCH,
  INVALID,
};

struct ColdLinkResult {
  bool ok = false;
  NavSnapshot stack;
  bool jsExecuted = false;
  ColdLinkError error = ColdLinkError::INVALID;
};

struct PerfSample {
  long long micros = 0;
  bool withinBudget = false;
};

// Cold-link zero-JS resolution (FR-NAV-005 / AC-NAV-012). Parses the URI,
// matches the default compiled manifest (muse://home, muse://details/42),
// and pushes the full parent chain as one NavGraph transaction. Never runs
// JS: jsExecuted is always false, including on miss.
ColdLinkResult resolveColdLink(const std::string& uri);

// Times fn via std::chrono; withinBudget == (micros < 4000).
PerfSample measureTransition(std::function<void()> fn);
