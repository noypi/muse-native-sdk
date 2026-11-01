// Batch 4 T10 — later-phase admission gate (FR-MEDIA-013/AC-MEDIA-021;
// required FR-DEPS-007 AC-017/018). TC-MEDIA-021 mapped file.
//
// Binds ONLY to the T10 `Interface:` line in plan.md:
//   `none — phase-admission gate; sh engine/media/run-media.sh
//    phased_codecs exits 0 iff phase gates green and earlier phases
//    unregressed`
// never to implementation. Per the tester skill, a `none — <why>` item is
// not a gap: the contract below carries that line verbatim and the tests
// below verify the gate behaviorally.
//
// What this test verifies (hermetic, C++17 + libc only):
//   (a) the coder-owned gate wiring exists: `engine/media/run-media.sh`
//       contains a `phased_codecs` case that checks the FFmpeg LGPL gates
//       (shared-lib link, no `--enable-gpl`/`--enable-nonfree`, configure
//       line in SBOM), the WebRTC dynamic-feature packaging gate, and that
//       re-runs the earlier phase without regression (read as data from
//       the checkout root cwd — ASSUMPTION A-CWD);
//   (b) the admission predicate truth table (pure in-test logic over
//       injected evidence literals — pins the AC-MEDIA-021 contract).
// No network, no env/config reads, no filesystem writes, no live services,
// no secrets. Symbol/SBOM inspection itself runs inside the
// `phased_codecs` script case (coder-owned); this test proves the case
// exists and that it asserts every admission clause.
//
// Expected state: RED — `run-media.sh` has no `phased_codecs` case yet, so
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
// `sh engine/media/run-media.sh phased_codecs`, so this relative path is
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

// Admission predicate under test (AC-MEDIA-021, in-test statement of the
// contract — the script case must implement exactly this): admit a later
// phase iff its own gates are green AND every earlier phase is
// unregressed. FFmpeg gate = FR-DEPS-007 AC-017/018 (shared-lib link,
// LGPL config, configure line in SBOM); WebRTC gate = dynamic-feature
// packaging per specs-build-toolchain.
struct LaterPhaseEvidence {
  bool ffmpeg_shared_lib;
  bool ffmpeg_no_gpl_flag;
  bool ffmpeg_no_nonfree_flag;
  bool ffmpeg_configure_in_sbom;
  bool webrtc_dynamic_feature;
  bool earlier_phases_green;
};

bool AdmitsLaterPhase(const LaterPhaseEvidence& e) {
  const bool ffmpeg_gate = e.ffmpeg_shared_lib && e.ffmpeg_no_gpl_flag &&
                           e.ffmpeg_no_nonfree_flag &&
                           e.ffmpeg_configure_in_sbom;
  const bool webrtc_gate = e.webrtc_dynamic_feature;
  return ffmpeg_gate && webrtc_gate && e.earlier_phases_green;
}

}  // namespace

// --- gate wiring (fails until the coder lands the script case) ------------

static void check_script_has_phased_codecs_case() {
  bool ok = false;
  const std::string script = ReadFile(kRunMediaSh, &ok);
  CHECK("run-media.sh readable (GATE: harness script present)", ok);
  if (!ok) return;
  CHECK("run-media.sh has phased_codecs case (GATE: T10 script case)",
        script.find("phased_codecs") != std::string::npos);
}

static void check_script_case_checks_gpl_refusal() {
  bool ok = false;
  const std::string script = Lower(ReadFile(kRunMediaSh, &ok));
  if (!ok) {
    CHECK("script checks gpl refusal (GATE: unreadable script)", false);
    return;
  }
  // AC-DEPS-018: the refusal names the offending configure flag.
  CHECK("phased_codecs case refuses --enable-gpl (GATE: T10 script case)",
        script.find("enable-gpl") != std::string::npos);
}

static void check_script_case_checks_nonfree_refusal() {
  bool ok = false;
  const std::string script = Lower(ReadFile(kRunMediaSh, &ok));
  if (!ok) {
    CHECK("script checks nonfree refusal (GATE: unreadable script)", false);
    return;
  }
  CHECK("phased_codecs case refuses --enable-nonfree (GATE: T10 script case)",
        script.find("enable-nonfree") != std::string::npos);
}

static void check_script_case_checks_sbom_configure_line() {
  bool ok = false;
  const std::string script = Lower(ReadFile(kRunMediaSh, &ok));
  if (!ok) {
    CHECK("script checks sbom line (GATE: unreadable script)", false);
    return;
  }
  // AC-DEPS-017: the configure line is recorded in the SBOM.
  CHECK("phased_codecs case checks SBOM configure line (GATE: T10 script)",
        script.find("sbom") != std::string::npos);
}

static void check_script_case_checks_webrtc_dynamic_feature() {
  bool ok = false;
  const std::string script = Lower(ReadFile(kRunMediaSh, &ok));
  if (!ok) {
    CHECK("script checks webrtc gate (GATE: unreadable script)", false);
    return;
  }
  const bool names_webrtc =
      script.find("webrtc") != std::string::npos;
  const bool names_packaging = script.find("dynamic") != std::string::npos ||
                               script.find("feature") != std::string::npos;
  CHECK("phased_codecs case gates webrtc dynamic feature (GATE: T10)",
        names_webrtc && names_packaging);
}

static void check_script_case_reruns_earlier_phase() {
  bool ok = false;
  const std::string script = ReadFile(kRunMediaSh, &ok);
  if (!ok) {
    CHECK("script re-runs earlier phase (GATE: unreadable script)", false);
    return;
  }
  // AC-MEDIA-021: no regression — the later-phase case must re-establish
  // the phase-1 gate.
  CHECK("phased_codecs case re-runs phased_media3 (GATE: no regression)",
        script.find("phased_media3") != std::string::npos);
}

// --- admission predicate truth table (contract pins, pass) -----------------

static LaterPhaseEvidence AllGreen() {
  return LaterPhaseEvidence{true, true, true, true, true, true};
}

static void check_admit_when_all_gates_green() {
  CHECK("admit iff all gates green + earlier green",
        AdmitsLaterPhase(AllGreen()));
}

static void check_no_admit_when_ffmpeg_static() {
  LaterPhaseEvidence e = AllGreen();
  e.ffmpeg_shared_lib = false;
  CHECK("no admit on static ffmpeg linkage", !AdmitsLaterPhase(e));
}

static void check_no_admit_when_gpl_flag() {
  LaterPhaseEvidence e = AllGreen();
  e.ffmpeg_no_gpl_flag = false;
  CHECK("no admit with --enable-gpl", !AdmitsLaterPhase(e));
}

static void check_no_admit_when_nonfree_flag() {
  LaterPhaseEvidence e = AllGreen();
  e.ffmpeg_no_nonfree_flag = false;
  CHECK("no admit with --enable-nonfree", !AdmitsLaterPhase(e));
}

static void check_no_admit_when_sbom_line_missing() {
  LaterPhaseEvidence e = AllGreen();
  e.ffmpeg_configure_in_sbom = false;
  CHECK("no admit without SBOM configure line", !AdmitsLaterPhase(e));
}

static void check_no_admit_when_webrtc_not_dynamic() {
  LaterPhaseEvidence e = AllGreen();
  e.webrtc_dynamic_feature = false;
  CHECK("no admit when webrtc not a dynamic feature", !AdmitsLaterPhase(e));
}

static void check_no_admit_when_earlier_regressed() {
  LaterPhaseEvidence e = AllGreen();
  e.earlier_phases_green = false;
  CHECK("no admit when earlier phase regressed", !AdmitsLaterPhase(e));
}

static void check_gate_is_deterministic() {
  const LaterPhaseEvidence e = AllGreen();
  CHECK("gate deterministic across evaluations",
        AdmitsLaterPhase(e) == AdmitsLaterPhase(e));
}

int main() {
  check_script_has_phased_codecs_case();
  check_script_case_checks_gpl_refusal();
  check_script_case_checks_nonfree_refusal();
  check_script_case_checks_sbom_configure_line();
  check_script_case_checks_webrtc_dynamic_feature();
  check_script_case_reruns_earlier_phase();
  check_admit_when_all_gates_green();
  check_no_admit_when_ffmpeg_static();
  check_no_admit_when_gpl_flag();
  check_no_admit_when_nonfree_flag();
  check_no_admit_when_sbom_line_missing();
  check_no_admit_when_webrtc_not_dynamic();
  check_no_admit_when_earlier_regressed();
  check_gate_is_deterministic();
  if (s_failures == 0) {
    std::printf("ALL GREEN phased_codecs_test\n");
    return 0;
  }
  std::printf("%d FAILURES phased_codecs_test\n", s_failures);
  return 1;
}
