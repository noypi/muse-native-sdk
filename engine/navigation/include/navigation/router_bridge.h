#pragma once

#include <string>
#include <vector>

#include "navigation/graph.h"

enum class BridgeError {
  NONE,
  UNKNOWN_METHOD,
  BAD_ARGS,
  NAV_ERROR,
};

struct BridgeResult {
  bool ok = false;
  std::string json;
  BridgeError error = BridgeError::NONE;
};

// JSI-shaped router bridge composing the T1 NavGraph. Snapshot/restore
// delegate to the T1/T6 canonical NAV1 format so nav state rides the
// STATE-PERSIST / LIFE-SAVE envelope.
class RouterBridge {
 public:
  explicit RouterBridge(std::vector<std::string> routes);

  BridgeResult invoke(const std::string& method, const std::string& jsonArgs);
  std::string snapshotForPersist() const;
  bool restoreFromLifeSave(const std::string& blob);

 private:
  std::vector<std::string> routes_;
  NavGraph graph_;
};
