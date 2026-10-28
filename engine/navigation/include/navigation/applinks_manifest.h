#pragma once

#include <string>
#include <vector>

#include "navigation/trie.h"

// Single-source App Links route manifest (FR-NAV-006 / AC-NAV-013).
// Field names mirror engine/navigation/applinks_manifest.schema.json;
// route-descriptor names (host, routeId, query, QueryParam{name, type,
// required}) reuse include/navigation/trie.h.

struct ManifestRoute {
  std::string host;
  std::string pathPattern;
  std::string routeId;
  std::vector<QueryParam> query;
};

struct RouteManifest {
  std::vector<std::string> hosts;
  std::vector<std::string> schemes;
  std::vector<ManifestRoute> routes;
  std::string packageName;
  std::vector<std::string> sha256CertFingerprints;
  bool autoVerify = true;
};

enum class ManifestErrorReason {
  UNKNOWN_HOST,
  BAD_PATTERN,
  MISSING_FINGERPRINT,
};

struct ManifestError {
  ManifestErrorReason reason = ManifestErrorReason::BAD_PATTERN;
  std::string detail;
};

struct LoadManifestResult {
  bool ok = false;
  RouteManifest manifest;
  ManifestError error;
};

// Pure parse+validate; no I/O beyond reading the file, no network.
// Never throws: I/O and parse failures are reported via `ok == false`.
LoadManifestResult LoadManifest(const std::string& path);
