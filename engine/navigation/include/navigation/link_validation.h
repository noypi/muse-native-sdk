#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "navigation/trie.h"

struct LinkCandidate {
  std::string scheme;
  std::string host;
  std::vector<std::string> path;
  std::map<std::string, std::string> query;
  bool assetLinksVerified = false;
};

struct LinkAllowlist {
  std::vector<std::string> schemes;
  std::vector<RouteEntry> routes;
  std::size_t maxParamBytes = 1024;
  std::size_t maxUriBytes = 4096;
};

enum class LinkError {
  NONE,
  UNVERIFIED_HOST,
  UNKNOWN_ROUTE,
  MALFORMED_URI,
  CREDENTIALS_PRESENT,
  TRAVERSAL_DETECTED,
  OVERSIZED,
  SCHEMA_MISMATCH,
};

struct LinkVerdict {
  bool allowed = false;
  LinkError error = LinkError::UNKNOWN_ROUTE;
};

// Hermetic const query: validates a deep-link candidate against the compiled
// allowlist. Returns {true, NONE} only for verified, schema-validated links;
// every false verdict navigates nowhere, commits nothing, and throws nothing.
// Check order: size caps → scheme allowlist → credentials/traversal scan
// (raw + percent-decoded) → host+path allowlist match → asset-links flag.
LinkVerdict ValidateLink(const LinkCandidate& in,
                         const LinkAllowlist& allowlist) noexcept;
