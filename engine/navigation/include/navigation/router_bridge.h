#pragma once

#include <string>
#include <vector>

#include "navigation/graph.h"
#include "navigation/snapshot.h"

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

  // Optional T2 content providers (caller-supplied lists/text hooks). When
  // unset, snapshotForPersist emits the bare T1 canonical blob; when set,
  // content sections ride the blob via captureSnapshotWithContent.
  void setScrollProvider(ScrollProvider provider);
  void setFocusProvider(FocusProvider provider);
  // Optional T5 commit-gate hooks (FR-NAV-006 validator, FR-NAV-003 guard).
  // Null/empty hooks allow every route, preserving pre-gate behavior.
  void setValidateHook(ValidateFn validate);
  void setGuardHook(GuardFn guard);

 private:
  std::vector<std::string> routes_;
  NavGraph graph_;
  ScrollProvider scroll_;
  FocusProvider focus_;
  ValidateFn validate_;
  GuardFn guard_;
};
