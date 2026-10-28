#include "navigation/cold_link.h"

#include <chrono>
#include <utility>
#include <vector>

#include "navigation/graph.h"

namespace {

constexpr long long kTransitionBudgetMicros = 4000;

ColdLinkResult Fail(ColdLinkError error) {
  ColdLinkResult r;
  r.ok = false;
  r.jsExecuted = false;
  r.error = error;
  return r;
}

}  // namespace

ColdLinkResult resolveColdLink(const std::string& uri) {
  if (uri.empty()) return Fail(ColdLinkError::INVALID);
  const std::string sep = "://";
  const std::size_t sepPos = uri.find(sep);
  if (sepPos == std::string::npos || sepPos == 0) {
    return Fail(ColdLinkError::INVALID);
  }
  const std::string scheme = uri.substr(0, sepPos);
  if (scheme != "muse") return Fail(ColdLinkError::INVALID);
  std::string rest = uri.substr(sepPos + sep.size());
  if (rest.empty()) return Fail(ColdLinkError::INVALID);

  // Strip query/fragment; match on host + path only.
  rest = rest.substr(0, rest.find_first_of("?#"));
  if (rest.empty()) return Fail(ColdLinkError::INVALID);

  std::string host;
  std::vector<std::string> segments;
  const std::size_t slash = rest.find('/');
  if (slash == std::string::npos) {
    host = rest;
  } else {
    host = rest.substr(0, slash);
    std::string path = rest.substr(slash + 1);
    std::string cur;
    for (char c : path) {
      if (c == '/') {
        if (!cur.empty()) segments.push_back(cur);
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) segments.push_back(cur);
  }
  if (host.empty()) return Fail(ColdLinkError::INVALID);

  // Default compiled manifest: home + details (details accepts an id).
  bool isHome = (host == "home" && segments.empty());
  bool isDetails = (host == "details" && segments.size() <= 1);
  if (!isHome && !isDetails) return Fail(ColdLinkError::NO_MATCH);

  // Full parent-chain push as one NavGraph transaction; graph roots at
  // home so the nested blob carries both route ids.
  NavGraph graph({"home", "details"});
  if (isDetails) {
    NavOp op;
    op.type = NavOpType::Push;
    op.routeId = "details";
    NavResult applied = graph.apply(op);
    if (!applied.ok) return Fail(ColdLinkError::NO_MATCH);
  }

  ColdLinkResult r;
  r.ok = true;
  r.stack.version = kNavStateVersion;
  r.stack.graphBlob = graph.serialize();
  r.jsExecuted = false;
  r.error = ColdLinkError::NONE;
  return r;
}

PerfSample measureTransition(std::function<void()> fn) {
  const auto start = std::chrono::steady_clock::now();
  if (fn) fn();
  const auto end = std::chrono::steady_clock::now();
  const long long micros =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();
  PerfSample s;
  s.micros = micros;
  s.withinBudget = (micros < kTransitionBudgetMicros);
  return s;
}
