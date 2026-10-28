// T6 contract tests — FR-NAV-004 / AC-NAV-007 (TC-NAV-007).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Consults no implementation;
// binds to the T6 `Interface:` line verbatim (see unit-tests/T6.md).
// Reuses T1's canonical serialize format `NAV1;v=1;...` (see
// `navigation/graph.h` + `src/graph.cpp`: prefix "NAV1;", version token
// "v=1;", fields "sel=<n>;tabs=<csv|c...>;modals=<csv>;drawer=<0|1>").
//
// ASSUMPTION (NavSnapshot shape): the plan names `NavSnapshot {
// uint32_t version; NavGraphState graph; }` but `NavGraphState` is not
// defined anywhere. These cases use the test's proposal `NavSnapshot {
// uint32_t version; std::string graphBlob; }` where `graphBlob` is a T1
// canonical blob and `version` is `kNavStateVersion` (== 1). Field names
// are the proposal; reconcile call sites, not expectations, if the
// coder names them differently. `homeSnapshot` means a snapshot whose
// graphBlob is the freshly constructed `NavGraph(routes).serialize()`.
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "navigation/graph.h"
#include "navigation/state_schema.h"

namespace {

int failures = 0;

#define EXPECT_TRUE(cond, name)                       \
  do {                                                \
    if (!(cond)) {                                    \
      std::printf("FAIL %s\n", name);                 \
      ++failures;                                     \
    } else {                                          \
      std::printf("PASS %s\n", name);                 \
    }                                                 \
  } while (0)

const std::vector<std::string> kRoutes = {"home", "details", "settings"};

NavSnapshot makeSnapshot(const std::string& graphBlob) {
  NavSnapshot s;
  s.version = kNavStateVersion;
  s.graphBlob = graphBlob;
  return s;
}

std::string homeBlob() {
  NavGraph g(kRoutes);
  return g.serialize();
}

bool check_encode_decode_round_trip() {
  NavGraph g(kRoutes);
  NavOp push{NavOpType::Push, "details", 0};
  if (!g.apply(push).ok) return false;
  EncodeResult e = encodeState(makeSnapshot(g.serialize()));
  if (e.blob.empty()) return false;
  DecodeResult d = decodeState(e.blob);
  return d.ok && d.error == DecodeError::NONE && !d.fellBackToHome &&
         d.snapshot.graphBlob == g.serialize();
}

bool check_encoded_blob_reuses_canonical_format() {
  NavGraph g(kRoutes);
  EncodeResult e = encodeState(makeSnapshot(g.serialize()));
  // T1 canonical prefix reused verbatim.
  return e.blob.rfind("NAV1;v=1;", 0) == 0;
}

bool check_unknown_route_degrades_to_home() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  std::string blob = g.serialize();
  // Swap a live route for a removed one inside the canonical body.
  std::string tampered = blob;
  std::string::size_type pos = tampered.find("details");
  if (pos == std::string::npos) return false;
  tampered.replace(pos, 7, "removed_x");
  EncodeResult e = encodeState(makeSnapshot(tampered));
  DecodeResult d = decodeState(e.blob.empty() ? tampered : e.blob);
  return d.ok && d.error == DecodeError::UNKNOWN_ROUTE &&
         d.fellBackToHome && d.snapshot.graphBlob == homeBlob();
}

bool check_corrupt_blob_never_throws() {
  bool noThrow = true;
  DecodeResult d{false, NavSnapshot{}, DecodeError::CORRUPT, false};
  try {
    d = decodeState("not-a-nav-blob\x00\x01");
  } catch (...) {
    noThrow = false;
  }
  return noThrow && !d.ok && d.error == DecodeError::CORRUPT;
}

bool check_empty_blob_is_corrupt() {
  DecodeResult d = decodeState("");
  return !d.ok && d.error == DecodeError::CORRUPT;
}

bool check_future_version_reports_bad_version() {
  NavSnapshot s = makeSnapshot(homeBlob());
  s.version = kNavStateVersion + 1;
  EncodeResult e = encodeState(s);
  DecodeResult d = decodeState(e.blob);
  return d.error == DecodeError::BAD_VERSION && d.fellBackToHome;
}

bool check_truncated_blob_is_corrupt() {
  NavGraph g(kRoutes);
  EncodeResult e = encodeState(makeSnapshot(g.serialize()));
  if (e.blob.size() < 4) return false;
  DecodeResult d = decodeState(e.blob.substr(0, 4));
  return !d.ok && d.error == DecodeError::CORRUPT;
}

}  // namespace

int main() {
  EXPECT_TRUE(check_encode_decode_round_trip(), "encode_decode_round_trip");
  EXPECT_TRUE(check_encoded_blob_reuses_canonical_format(),
              "encoded_blob_reuses_canonical_format");
  EXPECT_TRUE(check_unknown_route_degrades_to_home(),
              "unknown_route_degrades_to_home");
  EXPECT_TRUE(check_corrupt_blob_never_throws(), "corrupt_blob_never_throws");
  EXPECT_TRUE(check_empty_blob_is_corrupt(), "empty_blob_is_corrupt");
  EXPECT_TRUE(check_future_version_reports_bad_version(),
              "future_version_reports_bad_version");
  EXPECT_TRUE(check_truncated_blob_is_corrupt(), "truncated_blob_is_corrupt");
  if (failures > 0) {
    std::printf("state_schema_test: %d failing\n", failures);
    return 1;
  }
  std::printf("state_schema_test: all passing\n");
  return 0;
}
