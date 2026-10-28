#pragma once

#include <functional>
#include <map>
#include <string>

#include "navigation/trie.h"

enum class ResultError {
  NONE,
  TARGET_GONE,
  TYPE_MISMATCH,
};

struct ResultDelivery {
  bool delivered = false;
  ResultError error = ResultError::TARGET_GONE;
};

// Live typed-result destinations scoped to the back stack (FR-NAV-007 /
// AC-NAV-019). Each registered destination declares the TypedParam type it
// accepts; deliverResult type-checks before invoking the callback, and
// onPopped invalidates the destination so no stale callback can fire.
class ResultHost {
 public:
  void registerDestination(const std::string& routeId, ParamType type,
                           std::function<void(const TypedParam&)> callback);

 private:
  friend ResultDelivery deliverResult(ResultHost& h,
                                       const std::string& toRoute,
                                       const TypedParam& value);
  friend void onPopped(ResultHost& h, const std::string& routeId);

  struct Entry {
    ParamType type = ParamType::String;
    std::function<void(const TypedParam&)> callback;
  };
  std::map<std::string, Entry> destinations_;
};

ResultDelivery deliverResult(ResultHost& h, const std::string& toRoute,
                             const TypedParam& value);
void onPopped(ResultHost& h, const std::string& routeId);
