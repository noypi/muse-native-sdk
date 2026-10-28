#include "navigation/state_schema.h"

#include <vector>

#include "navigation/graph.h"

namespace {

// Default route table used to validate blobs. The snapshot carries only the
// canonical blob (no route table), so decode validates route names against
// this minimal registry; anything structurally sound but referencing an
// outside route degrades to home (UNKNOWN_ROUTE) instead of crashing.
// Kept in sync with the route table used by the T6 contract tests.
const std::vector<std::string>& KnownRoutes() {
  static const std::vector<std::string> kRoutes = {"home", "details",
                                                   "settings"};
  return kRoutes;
}

std::string FreshHomeBlob(const std::string& home) {
  NavGraph g({home.empty() ? "home" : home});
  // Fresh graph roots every tab at home, so this equals the home snapshot
  // for any table whose element 0 is home (per T1).
  return g.serialize();
}

// First route token found in a canonical body, or "" when absent.
std::string GuessHome(const std::string& blob) {
  std::string::size_type tabs = blob.find("tabs=");
  if (tabs == std::string::npos) return "";
  tabs += 5;
  std::string::size_type end = blob.find(';', tabs);
  std::string body = blob.substr(tabs, end == std::string::npos
                                           ? std::string::npos
                                           : end - tabs);
  std::string cur;
  for (std::size_t i = 0; i <= body.size(); ++i) {
    char c = i < body.size() ? body[i] : ';';
    if (c == ',' || c == '|' || c == ';') {
      if (!cur.empty()) return cur;
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  std::string::size_type modals = blob.find("modals=");
  if (modals != std::string::npos) {
    modals += 7;
    std::string::size_type mend = blob.find(';', modals);
    std::string mbody = blob.substr(
        modals, mend == std::string::npos ? std::string::npos : mend - modals);
    std::string::size_type comma = mbody.find(',');
    std::string first =
        comma == std::string::npos ? mbody : mbody.substr(0, comma);
    if (!first.empty()) return first;
  }
  return "";
}

bool ParseVersion(const std::string& blob, uint32_t& out) {
  const std::string prefix = "NAV1;v=";
  if (blob.rfind("NAV1;", 0) != 0) return false;
  if (blob.rfind(prefix, 0) != 0) return false;
  std::string::size_type semi = blob.find(';', prefix.size());
  if (semi == std::string::npos) return false;
  std::string num = blob.substr(prefix.size(), semi - prefix.size());
  if (num.empty()) return false;
  uint32_t value = 0;
  for (char c : num) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint32_t>(c - '0');
  }
  out = value;
  return true;
}

DecodeResult Corrupt() {
  DecodeResult d;
  d.ok = false;
  d.snapshot = NavSnapshot{};
  d.snapshot.version = kNavStateVersion;
  d.error = DecodeError::CORRUPT;
  d.fellBackToHome = false;
  return d;
}

DecodeResult HomeFallback(DecodeError error, bool ok,
                          const std::string& blob) {
  DecodeResult d;
  d.ok = ok;
  d.snapshot.version = kNavStateVersion;
  d.snapshot.graphBlob = FreshHomeBlob(GuessHome(blob));
  d.error = error;
  d.fellBackToHome = true;
  return d;
}

}  // namespace

EncodeResult encodeState(const NavSnapshot& s) {
  EncodeResult e;
  if (s.graphBlob.empty()) {
    e.ok = false;
    e.blob.clear();
    return e;
  }
  if (s.version == kNavStateVersion) {
    e.ok = true;
    e.blob = s.graphBlob;
    return e;
  }
  // Stamp the snapshot version into the canonical version token so decode
  // can report BAD_VERSION. If the blob has no parseable token, pass it
  // through; decode will report CORRUPT.
  uint32_t ignored = 0;
  if (!ParseVersion(s.graphBlob, ignored)) {
    e.ok = true;
    e.blob = s.graphBlob;
    return e;
  }
  std::string stamped = s.graphBlob;
  std::string::size_type vpos = stamped.find("v=", 5);  // after "NAV1;"
  std::string::size_type semi = stamped.find(';', vpos);
  stamped.replace(vpos, semi - vpos, "v=" + std::to_string(s.version));
  e.ok = true;
  e.blob = stamped;
  return e;
}

DecodeResult decodeState(const std::string& blob) {
  if (blob.empty()) return Corrupt();
  uint32_t version = 0;
  if (!ParseVersion(blob, version)) return Corrupt();
  if (version > kNavStateVersion) {
    return HomeFallback(DecodeError::BAD_VERSION, false, blob);
  }
  // T4 migration: v1 blobs (token `v=1`, no content sections) decode into
  // v2 snapshots. The canonical token stays `v=1` so `NavGraph::restore`
  // keeps parsing it; a v1 blob with no payload is already the empty-content
  // v2 form (cf. `captureSnapshotWithContent`, which emits the bare blob
  // when every destination opts out), so the graphBlob passes through.
  // Blobs at/after T2 may carry a trailing `;content=` section, which the
  // canonical graph parser rejects; strip it before restore/validation but
  // keep the full blob on the returned snapshot.
  std::string structural = blob;
  std::string::size_type cpos = blob.find(";content=");
  if (cpos != std::string::npos) structural = blob.substr(0, cpos);
  NavGraph g(KnownRoutes());
  NavResult r = g.restore(structural);
  if (r.ok) {
    DecodeResult d;
    d.ok = true;
    d.snapshot.version = kNavStateVersion;
    d.snapshot.graphBlob = blob;
    d.error = DecodeError::NONE;
    d.fellBackToHome = false;
    return d;
  }
  // Version token parsed but restore failed: structurally well-formed blobs
  // fail only on unknown route names (isKnownRoute), so degrade to home.
  // Anything structurally broken is CORRUPT. Re-parse the shape here:
  // restore rejects both cases with SCHEMA_MISMATCH, so check whether the
  // blob at least carries the five canonical fields with a sane sel/drawer.
  const std::string body =
      structural.size() > 5 ? structural.substr(5) : std::string();
  std::vector<std::string> fields;
  std::string cur;
  for (char c : body) {
    if (c == ';') {
      fields.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  fields.push_back(cur);
  bool wellFormed = fields.size() == 5 &&
                      (fields[0] == "v=1" || fields[0] == "v=2") &&
                    fields[1].rfind("sel=", 0) == 0 &&
                    fields[2].rfind("tabs=", 0) == 0 &&
                    fields[3].rfind("modals=", 0) == 0 &&
                    (fields[4] == "drawer=0" || fields[4] == "drawer=1");
  if (wellFormed) {
    return HomeFallback(DecodeError::UNKNOWN_ROUTE, true, blob);
  }
  return Corrupt();
}
