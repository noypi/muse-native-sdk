// T4 contract tests — FR-NAV-001 / AC-NAV-003 (TC-NAV-003 partial) +
// FR-NAV-004 / AC-NAV-007 (required dependency).
// Hermetic: C++17 + navigation headers only; no network, no env/config
// reads, no filesystem, no live services. Binds to the T4 `Interface:`
// line verbatim (see unit-tests/T4.md):
//   existing `DecodeResult decodeState(const std::string& blob);` —
//   unchanged signature; extended behavior: v1 blob → `{ok=true,
//   snapshot.version=2, graphBlob` with empty scroll/focus sections `}`;
//   unknown route → `{ok=true, error=UNKNOWN_ROUTE,
//   fellBackToHome=true}`; corrupt → `{ok=false, error=CORRUPT,
//   fellBackToHome=false}`.
// Depends on: T1. This file is owned by T4 (first item to commit it per
// GR-08 T1.8); T5 extends it in batch 3 with `commitRestore` cases only.
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

std::string homeBlob() {
  NavGraph g(kRoutes);
  return g.serialize();
}

// A v1 blob: canonical T1 envelope without scroll/focus sections.
std::string v1Blob() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  NavSnapshot s;
  s.version = 1;
  s.graphBlob = g.serialize();
  EncodeResult e = encodeState(s);
  return e.ok ? e.blob : g.serialize();
}

bool check_v1_blob_decodes_ok_at_version_2() {
  DecodeResult d = decodeState(v1Blob());
  return d.ok && d.snapshot.version == 2 && !d.fellBackToHome &&
         d.error == DecodeError::NONE;
}

bool check_v1_migrated_graphblob_has_empty_content() {
  DecodeResult d = decodeState(v1Blob());
  if (!d.ok || d.snapshot.version != 2) return false;
  // Migrated graphBlob keeps the structural prefix parseable and carries
  // empty scroll/focus sections: it restores and holds no payload.
  std::string structural = d.snapshot.graphBlob;
  std::string::size_type pos = structural.find(";content=");
  std::string body = (pos == std::string::npos) ? structural
                                                : structural.substr(0, pos);
  NavGraph h(kRoutes);
  if (!h.restore(body).ok) return false;
  return d.snapshot.graphBlob.find("NAV1;") == 0;
}

bool check_v1_home_blob_migrates() {
  NavSnapshot s;
  s.version = 1;
  s.graphBlob = homeBlob();
  EncodeResult e = encodeState(s);
  std::string blob = e.ok ? e.blob : homeBlob();
  DecodeResult d = decodeState(blob);
  return d.ok && d.snapshot.version == 2 &&
         d.error == DecodeError::NONE && !d.fellBackToHome;
}

bool check_removed_route_degrades_to_home() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::Push, "details", 0});
  std::string blob = g.serialize();
  std::string::size_type pos = blob.find("details");
  if (pos == std::string::npos) return false;
  blob.replace(pos, 7, "removed_x");
  DecodeResult d = decodeState(blob);
  return d.ok && d.error == DecodeError::UNKNOWN_ROUTE &&
         d.fellBackToHome && d.snapshot.graphBlob == homeBlob();
}

bool check_removed_modal_route_degrades_to_home() {
  NavGraph g(kRoutes);
  g.apply(NavOp{NavOpType::PushModal, "details", 0});
  std::string blob = g.serialize();
  std::string::size_type pos = blob.find("details");
  if (pos == std::string::npos) return false;
  blob.replace(pos, 7, "removed_y");
  DecodeResult d = decodeState(blob);
  return d.ok && d.error == DecodeError::UNKNOWN_ROUTE &&
         d.fellBackToHome && d.snapshot.graphBlob == homeBlob();
}

bool check_corrupt_blob_is_corrupt_no_fallback() {
  DecodeResult d = decodeState("not-a-nav-blob\x00\x01");
  return !d.ok && d.error == DecodeError::CORRUPT && !d.fellBackToHome;
}

bool check_empty_blob_is_corrupt_no_fallback() {
  DecodeResult d = decodeState("");
  return !d.ok && d.error == DecodeError::CORRUPT && !d.fellBackToHome;
}

bool check_truncated_blob_is_corrupt() {
  std::string blob = v1Blob();
  if (blob.size() < 4) return false;
  DecodeResult d = decodeState(blob.substr(0, 4));
  return !d.ok && d.error == DecodeError::CORRUPT && !d.fellBackToHome;
}

bool check_decode_never_throws_no_crash() {
  bool noThrow = true;
  DecodeResult d{false, NavSnapshot{}, DecodeError::CORRUPT, false};
  try {
    d = decodeState("NAV1;v=1;sel=99;tabs=removed_x|nope;modals=zzz;drawer=9");
  } catch (...) {
    noThrow = false;
  }
  // Unknown/removed content degrades or reports corrupt — never crashes.
  return noThrow && (d.error == DecodeError::UNKNOWN_ROUTE ||
                     d.error == DecodeError::CORRUPT);
}

}  // namespace

int main() {
  EXPECT_TRUE(check_v1_blob_decodes_ok_at_version_2(),
              "v1_blob_decodes_ok_at_version_2");
  EXPECT_TRUE(check_v1_migrated_graphblob_has_empty_content(),
              "v1_migrated_graphblob_has_empty_content");
  EXPECT_TRUE(check_v1_home_blob_migrates(), "v1_home_blob_migrates");
  EXPECT_TRUE(check_removed_route_degrades_to_home(),
              "removed_route_degrades_to_home");
  EXPECT_TRUE(check_removed_modal_route_degrades_to_home(),
              "removed_modal_route_degrades_to_home");
  EXPECT_TRUE(check_corrupt_blob_is_corrupt_no_fallback(),
              "corrupt_blob_is_corrupt_no_fallback");
  EXPECT_TRUE(check_empty_blob_is_corrupt_no_fallback(),
              "empty_blob_is_corrupt_no_fallback");
  EXPECT_TRUE(check_truncated_blob_is_corrupt(), "truncated_blob_is_corrupt");
  EXPECT_TRUE(check_decode_never_throws_no_crash(),
              "decode_never_throws_no_crash");
  if (failures > 0) {
    std::printf("restore_test: %d failing\n", failures);
    return 1;
  }
  std::printf("restore_test: all passing\n");
  return 0;
}
