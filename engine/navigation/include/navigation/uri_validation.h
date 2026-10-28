#pragma once

#include <map>
#include <string>

#include "navigation/applinks_manifest.h"
#include "navigation/trie.h"

// Ingress URI verification stage (FR-NAV-006 / AC-NAV-014). Runs before trie
// matching: scheme/host allowlist from the T1 manifest, hostile-input
// rejection, then typed-param coercion for the matched route.
constexpr int kMaxUriBytes = 4096;
constexpr int kMaxParamBytes = 1024;
constexpr int kMaxParams = 32;

enum class RejectReason {
  UNKNOWN_HOST,
  PATH_MISMATCH,
  BAD_PARAM,
  OVERSIZE,
  ENCODING,
  CREDENTIALS,
  TRAVERSAL,
};

struct ValidateResult {
  bool accepted = false;
  // Always false: no validated or rejected URI navigates by itself; the
  // caller dispatches separately on accept.
  bool navigate = false;
  std::string routeId;
  std::map<std::string, TypedParam> params;
  RejectReason reason = RejectReason::PATH_MISMATCH;
};

// Never throws: every failure mode returns a Reject with navigate == false.
ValidateResult ValidateUri(const std::string& uri, const RouteManifest& manifest);
