// Plugin capability manifest: load-time declaration + enforcement surface
// (NFR-CORE-006, AC-CORE-009).
//
// The manifest enumerates what a plugin may use: network access,
// filesystem roots, JNI classes, and thread affinity. The loader parses
// it at load time (parseManifest) and denies anything undeclared before
// plugin code runs (enforce). Enforcement is deny-by-default: every
// undeclared capability is denied with a named reason (ManifestError /
// deny reason string) surfaced to the sandbox peer. The policy decision
// itself stays with security (NFR-CORE-008, out of scope); this is only
// the declaration + enforcement surface.
#pragma once

#include <string>
#include <vector>

namespace muse {
namespace core {
namespace plugin {

// Load-time capability declaration for one plugin.
struct Manifest {
  bool allowNetwork;
  std::vector<std::string> fsRoots;
  std::vector<std::string> jniClasses;
  std::string threadAffinity;
};

// Machine-readable manifest failure codes.
enum class ManifestErrorCode {
  UNKNOWN_CAPABILITY,
  FS_ROOT_OUTSIDE_ALLOWLIST,
  JNI_CLASS_DENIED,
  THREAD_AFFINITY_INVALID,
};

// Structured parse/deny failure: code plus a human-readable message
// (the named reason surfaced to the sandbox peer).
struct ManifestError {
  ManifestErrorCode code;
  std::string message;
};

// Parse outcome: ok(Manifest) on a valid manifest, err(ManifestError)
// on any rejected declaration.
class ParseResult {
 public:
  static ParseResult ok(Manifest manifest);
  static ParseResult err(ManifestError error);

  bool success() const { return success_; }
  const Manifest& manifest() const { return manifest_; }
  const ManifestError& error() const { return error_; }

 private:
  bool success_ = false;
  Manifest manifest_;
  ManifestError error_;
};

// A single capability use requested by plugin code at load time.
// Enforcement compares each requested use against the Manifest and
// denies (deny-by-default) anything undeclared.
struct PluginRequest {
  // True when the plugin wants network access for this request.
  bool needsNetwork = false;
  // Filesystem path the plugin wants to open (empty = no fs use).
  std::string path;
  // JNI class the plugin wants to resolve (empty = no JNI use).
  std::string jniClass;
  // Thread affinity the plugin wants to run on (empty = no pinning).
  std::string threadAffinity;
  // Optional named capability; anything outside the known set
  // (network/fs/jni/affinity) is an unknown capability and is denied.
  std::string capability;
  // Named reason for the denial, filled by enforce on deny.
  // Surfaced to the sandbox peer for diagnostics.
  std::string reason;
};

ParseResult parseManifest(const char* json);

// Deny-by-default enforcement: returns true only when every requested
// capability was declared in the manifest; otherwise returns false
// (deny) and records the named reason. Undeclared capabilities are
// denied with UNKNOWN_CAPABILITY; out-of-allowlist fs roots with
// FS_ROOT_OUTSIDE_ALLOWLIST; unlisted JNI classes with JNI_CLASS_DENIED;
// invalid affinity with THREAD_AFFINITY_INVALID.
bool enforce(const Manifest& manifest, const PluginRequest& request);

// Same as above but also returns the structured denial carrying the
// named reason for the sandbox peer.
bool enforce(const Manifest& manifest, PluginRequest& request,
             ManifestError* error);

}  // namespace plugin
}  // namespace core
}  // namespace muse
