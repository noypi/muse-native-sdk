#pragma once

#include <string>

// Per-graph back dispatch (FR-NAV-010 / AC-NAV-030). Graphs register with a
// priority (modal=30 > stack=20 > tab=10); dispatchBack() returns the
// registered graphId with the highest priority, or "home" when none claims
// it. Equal priorities tie-break deterministically to the lexicographically
// smallest graphId. Re-registering an id replaces its priority. Unregistering removes the
// entry so a destroyed graph never intercepts back; unregistering an unknown
// id is a no-op.
void registerGraph(const std::string& graphId, int priority);
void unregisterGraph(const std::string& graphId);
std::string dispatchBack();
