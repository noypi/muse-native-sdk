#pragma once

#include <string>

#include "navigation/graph.h"

struct EntryIntent {
  std::string source;
  std::string uri;
  std::string routeId;
};

struct FunnelResult {
  bool ok = false;
  std::string routeId;
  bool deduped = false;
  NavError error = NavError::NONE;
};

// Single-router entry funnel (FR-NAV-010 / AC-NAV-029). All entry sources
// (launcher/deeplink/notification/service/onNewIntent) resolve through this
// one funnel; a repeat of an identical intent returns deduped=true instead
// of stacking a duplicate push. Dedup is keyed on the full intent
// (source+uri+routeId) and held process-global.
FunnelResult routeEntry(const EntryIntent& in);
