#pragma once

#include <cstdint>
#include <string>

constexpr uint32_t kNavStateVersion = 2;  // Envelope-only version: the wire
// token intentionally stays `v=1` so `NavGraph::restore` keeps parsing it;
// a `v=2` token on the wire is treated as unknown and degrades to home.

// T6 owns this definition (plan named an undefined `NavGraphState` member
// type): the graph is carried as a string blob in T1's canonical
// `NAV1;v=1;...` format (envelope v2, wire token `v=1`; see
// navigation/graph.h).
struct NavSnapshot {
  uint32_t version = kNavStateVersion;
  std::string graphBlob;
};

struct EncodeResult {
  bool ok = false;
  std::string blob;
};

enum class DecodeError {
  NONE,
  BAD_VERSION,
  CORRUPT,
  UNKNOWN_ROUTE,
};

struct DecodeResult {
  bool ok = false;
  NavSnapshot snapshot;
  DecodeError error = DecodeError::CORRUPT;
  bool fellBackToHome = false;
};

EncodeResult encodeState(const NavSnapshot& s);
DecodeResult decodeState(const std::string& blob);
