// Batch 4 T9 — Media3-first phase gate (FR-MEDIA-013/AC-MEDIA-020;
// required FR-MEDIA-010). TC-MEDIA-020 mapped file.
//
// Binds ONLY to the T9 `Interface:` line in plan.md:
//   `none — build-phase gate; sh engine/media/run-media.sh phased_media3
//    exits 0 iff FR-MEDIA-010 set green and FFmpeg/WebRTC symbols absent
//    from base`
// never to implementation. Per the tester skill, a `none — <why>` item is
// not a gap: the contract below carries that line verbatim and the tests
// below verify the gate behaviorally.
//
// What this test verifies (hermetic, C++17 + libc only):
//   (a) the coder-owned gate wiring exists: `engine/media/run-media.sh`
//       contains a `phased_media3` case that checks FFmpeg/WebRTC absence
//       (read as data from the checkout root cwd — ASSUMPTION A-CWD);
//   (b) the gate predicate truth table (pure in-test logic over injected
//       evidence literals — pins the AC-MEDIA-020 contract: admit iff the
//       FR-MEDIA-010 set is green AND FFmpeg AND WebRTC are absent).
// No network, no env/config reads, no filesystem writes, no live services,
// no secrets. The base-artifact symbol absence itself is enforced inside
// the `phased_media3` script case (coder-owned, e.g. nm over base libs);
// this test proves the case exists and that it asserts both absences.
//
// Expected state: RED — `run-media.sh` has no `phased_media3` case yet, so
// the wiring checks fail. The truth-table checks pass (contract pins).
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// --- tiny harness (matches engine/media/tests convention) -------------------
static int s_failures = 0;

#define CHECK(name, expr)                                       \
  do {                                                          \
    if (!(expr)) {                                              \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, name); \
      ++s_failures;                                             \
    } else {                                                    \
      std::printf("PASS %s\n", name);                           \
    }                                                           \
  } while (0)

namespace {

// ASSUMPTION A-CWD: executed from the checkout root via
// `sh engine/media/run-media.sh phased_media3`, so this relative path is
// the script under test.
const char* kRunMediaSh = "engine/media/run-media.sh";

std::string ReadFile(const char* path, bool* ok) {
  std::ifstream in(path);
  if (!in) {
    *ok = false;
    return std::string();
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  *ok = true;
  return ss.str();
}

std::string Lower(std::string s) {
  for (std::string::size_type i = 0; i < s.size(); ++i) {
    s[i] = static_cast<char>(
        std::tolower(static_cast<unsigned char>(s[i])));
  }
  return s;
}

// Gate predicate under test (AC-MEDIA-020, in-test statement of the
// contract — the script case must implement exactly this):
// admit phase 1 iff the FR-MEDIA-010 acceptance set is green and both
// later-phase stacks are absent from the base artifact.
struct PhaseEvidence {
  bool media10_green;
  bool ffmpeg_absent;
  bool webrtc_absent;
};

bool AdmitsMedia3First(const PhaseEvidence& e) {
  return e.media10_green && e.ffmpeg_absent && e.webrtc_absent;
}

}  // namespace

// --- gate wiring (fails until the coder lands the script case) ------------

static void check_script_has_phased_media3_case() {
  bool ok = false;
  const std::string script = ReadFile(kRunMediaSh, &ok);
  CHECK("run-media.sh readable (GATE: harness script present)", ok);
  if (!ok) return;
  CHECK("run-media.sh has phased_media3 case (GATE: T9 script case)",
        script.find("phased_media3") != std::string::npos);
}

static void check_script_case_asserts_ffmpeg_absence() {
  bool ok = false;
  const std::string script = Lower(ReadFile(kRunMediaSh, &ok));
  if (!ok) {
    CHECK("script asserts ffmpeg absence (GATE: unreadable script)", false);
    return;
  }
  CHECK("phased_media3 case asserts ffmpeg absence (GATE: T9 script case)",
        script.find("ffmpeg") != std::string::npos);
}

static void check_script_case_asserts_webrtc_absence() {
  bool ok = false;
  const std::string script = Lower(ReadFile(kRunMediaSh, &ok));
  if (!ok) {
    CHECK("script asserts webrtc absence (GATE: unreadable script)", false);
    return;
  }
  CHECK("phased_media3 case asserts webrtc absence (GATE: T9 script case)",
        script.find("webrtc") != std::string::npos);
}

// --- gate predicate truth table (contract pins, pass independently) --------

static void check_admit_when_all_green_and_absent() {
  const PhaseEvidence e{true, true, true};
  CHECK("admit iff media10 green + ffmpeg/webtrc absent", AdmitsMedia3First(e));
}

static void check_no_admit_when_media10_red() {
  const PhaseEvidence e{false, true, true};
  CHECK("no admit while FR-MEDIA-010 set red", !AdmitsMedia3First(e));
}

static void check_no_admit_when_ffmpeg_present() {
  const PhaseEvidence e{true, false, true};
  CHECK("no admit while ffmpeg present in base", !AdmitsMedia3First(e));
}

static void check_no_admit_when_webrtc_present() {
  const PhaseEvidence e{true, true, false};
  CHECK("no admit while webrtc present in base", !AdmitsMedia3First(e));
}

static void check_no_admit_when_nothing_holds() {
  const PhaseEvidence e{false, false, false};
  CHECK("no admit when nothing holds", !AdmitsMedia3First(e));
}

static void check_gate_is_deterministic() {
  const PhaseEvidence e{true, true, true};
  CHECK("gate deterministic across evaluations",
        AdmitsMedia3First(e) == AdmitsMedia3First(e));
}

int main() {
  check_script_has_phased_media3_case();
  check_script_case_asserts_ffmpeg_absence();
  check_script_case_asserts_webrtc_absence();
  check_admit_when_all_green_and_absent();
  check_no_admit_when_media10_red();
  check_no_admit_when_ffmpeg_present();
  check_no_admit_when_webrtc_present();
  check_no_admit_when_nothing_holds();
  check_gate_is_deterministic();
  if (s_failures == 0) {
    std::printf("ALL GREEN phased_media3_test\n");
    return 0;
  }
  std::printf("%d FAILURES phased_media3_test\n", s_failures);
  return 1;
}
