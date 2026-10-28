#pragma once

#include <optional>
#include <string>

#include "navigation/applinks_manifest.h"

// Deferred-link capture -> persist -> first-launch replay (FR-NAV-006 /
// AC-NAV-015). The store is a single file under the app-private dir; the
// path is injected so tests stay hermetic. Capture persists synchronously
// to disk before ack (fsync-equivalent) and is idempotent. Drain returns
// the stored link once on first launch, clears it, and re-runs T3
// ValidateUri before returning: an invalid deferred link drains to nullopt
// (landing fallback, never an error screen). Survives process restart
// between capture and drain (recreate the object on the same file).
class DeferredLinkStore {
 public:
  explicit DeferredLinkStore(const std::string& storePath);
  DeferredLinkStore(const std::string& storePath,
                    const RouteManifest& manifest);

  // Persists synchronously to disk; idempotent (overwrite with same bytes).
  void CaptureReferrer(const std::string& referrerUrl);

  // Returns the stored link once, then clears it. Empty store -> nullopt.
  // Never throws: I/O or validation failures drain to nullopt.
  std::optional<std::string> DrainPendingLaunch();

 private:
  std::string storePath_;
  RouteManifest manifest_;
};

// Extracts the deferred link from a Play Install Referrer value (SVC-GMS
// format: URL-encoded query string; the link rides in a `link` or
// `deep_link` param). Absent link param or malformed input -> nullopt.
// Never throws.
std::optional<std::string> ParseReferrer(const std::string& installReferrer);
