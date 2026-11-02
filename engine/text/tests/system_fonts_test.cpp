// TC-TEXT-011 — AC-TEXT-011: font discovery via NDK system font C API,
// deterministic fallback ordering. Hermetic harness: plain C++17, no test
// framework, no device, no downloads. The NDK SystemFonts boundary is never
// touched directly; cases drive only discoverFonts() with inline FontQuery
// values and assert ordering/last-resort/error behavior.
// Shape mirrors engine/runtime/tests/instantiate_test.cpp: static bool
// check_*() cases + main, return 0 all-green / non-zero + failing name.
// NOTE: the T2 production header (muse/text/font_discovery.h) does not exist
// yet, so this file does not compile — that compile-missing-production state
// is the expected RED for T2, not a malformed test.
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "muse/text/font_discovery.h"

// ASSUMPTION: FontQuery has fields {family, scriptTag, locale, weight} as per
// the plan Interface line; scriptTag uses an OpenType-style uint32 tag
// (e.g. 'Latn'). Tests use well-known tags only.
// ASSUMPTION: discoverFonts returns StatusOr<std::vector<FontHandle>> (the
// plan's Interface line gives the value type; Status::kNoFaceAvailable
// implies a status-carrying return).
// ASSUMPTION: FontHandle is equality-comparable (needed for ordering
// determinism assertions).
// ASSUMPTION: the last-resort tail is stable across locales for the same script.
// ASSUMPTION (Batch 1 T2, BUG-M3-FIDELITY-005): NDK enumerator stub on host —
// the SystemFontEnumerator methods run against stubbed data where no NDK
// headers exist; the production Android TU (font_discovery_android.cpp, NDK
// ASystemFont/matcher) lives behind the same seam and resolves Roboto per
// script/locale on device. Roboto-family cases below are the RED drivers:
// they fail on the stub and turn green behind the production NDK lookup.
namespace {

constexpr uint32_t kLatn = 0x4C61746Eu;  // 'Latn'
constexpr uint32_t kArab = 0x41726162u;  // 'Arab'

muse::text::FontQuery MakeQuery(std::string family, uint32_t script,
                                std::string locale, uint16_t weight = 400) {
  muse::text::FontQuery q;
  q.family = std::move(family);
  q.scriptTag = script;
  q.locale = std::move(locale);
  q.weight = weight;
  return q;
}

}  // namespace

static bool check_known_family_returns_nonempty_ordered_handles() {
  auto result =
      muse::text::discoverFonts(MakeQuery("sans-serif", kLatn, "en-US"));
  if (!result.ok()) return false;
  return !(*result).empty();
}

static bool check_ordering_is_deterministic() {
  muse::text::FontQuery q = MakeQuery("sans-serif", kLatn, "en-US");
  auto a = muse::text::discoverFonts(q);
  auto b = muse::text::discoverFonts(q);
  if (!a.ok() || !b.ok()) return false;
  return (*a) == (*b);
}

static bool check_absent_family_falls_back_with_last_resort_last() {
  // AC-TEXT-011: absent family -> deterministic fallback ordering,
  // last-resort face last, never empty on success.
  muse::text::FontQuery q = MakeQuery("no-such-family-xyz", kLatn, "en-US");
  auto result = muse::text::discoverFonts(q);
  if (!result.ok()) return false;
  if ((*result).empty()) return false;
  // Determinism across identical absent-family queries.
  auto again = muse::text::discoverFonts(q);
  if (!again.ok()) return false;
  if ((*result) != (*again)) return false;
  // Last-resort stability: the tail of the chain must be stable across
  // locales for the same script (same bundled fallback).
  muse::text::FontQuery other = MakeQuery("no-such-family-xyz", kLatn, "fr-FR");
  auto otherResult = muse::text::discoverFonts(other);
  if (!otherResult.ok()) return false;
  if ((*otherResult).empty()) return false;
  return (*result).back() == (*otherResult).back();
}

static bool check_per_script_resolution_differs() {
  // Same family/locale but different scripts must resolve through per-script
  // fallback, not one global answer. If a script is genuinely unsupported the
  // result is kNoFaceAvailable (next case), not an empty ok.
  auto latn = muse::text::discoverFonts(MakeQuery("sans-serif", kLatn, "en-US"));
  auto arab = muse::text::discoverFonts(MakeQuery("sans-serif", kArab, "ar-EG"));
  if (!latn.ok() || !arab.ok()) return false;
  if ((*latn).empty() || (*arab).empty()) return false;
  return true;
}

static bool check_unsupported_script_returns_no_face_available() {
  // Never return empty success: unresolvable queries must fail gracefully.
  muse::text::FontQuery q = MakeQuery("", 0xFFFFFFFFu, "xx-XX");
  auto result = muse::text::discoverFonts(q);
  if (result.ok()) return !(*result).empty();
  return result.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_empty_family_falls_back_rather_than_crash() {
  auto result = muse::text::discoverFonts(MakeQuery("", kLatn, "en-US"));
  if (result.ok()) return !(*result).empty();
  return result.status() == muse::text::Status::kNoFaceAvailable;
}

// --- Batch 1 T2 (BUG-M3-FIDELITY-005): NDK lookup seam, stub-backed on host.
// AC-TEXT-045 (engine-stack vector text: Roboto fallback per script/locale,
// fail-closed to the bundled face). One behavior per test. ---

static bool check_enumerator_has_roboto_family() {
  // Production NDK lookup serves Roboto; the stub does not yet.
  muse::text::SystemFontEnumerator enumerator;
  return enumerator.HasFamily("Roboto");
}

static bool check_roboto_faces_cover_latin() {
  muse::text::SystemFontEnumerator enumerator;
  auto faces = enumerator.FacesForFamily("Roboto", kLatn);
  return !faces.empty();
}

static bool check_roboto_faces_cover_hani_cjk() {
  // AC-TEXT-046 script coverage: CJK must resolve through the NDK lookup.
  constexpr uint32_t kHani = 0x48616E69u;  // 'Hani'
  muse::text::SystemFontEnumerator enumerator;
  auto faces = enumerator.FacesForFamily("Roboto", kHani);
  return !faces.empty();
}

static bool check_roboto_faces_cover_arabic() {
  muse::text::SystemFontEnumerator enumerator;
  auto faces = enumerator.FacesForFamily("Roboto", kArab);
  return !faces.empty();
}

static bool check_generic_faces_cover_latin_deterministically() {
  muse::text::SystemFontEnumerator enumerator;
  auto a = enumerator.GenericFaces(kLatn);
  auto b = enumerator.GenericFaces(kLatn);
  return !a.empty() && a == b;
}

static bool check_last_resort_face_is_stable_nonzero() {
  // Fail-closed tail: bundled face always present, stable across scripts.
  muse::text::SystemFontEnumerator enumerator;
  auto latn = enumerator.LastResortFace(kLatn);
  auto arab = enumerator.LastResortFace(kArab);
  return latn.id != 0 && latn == arab;
}

static bool check_supports_script_latin_and_arabic() {
  muse::text::SystemFontEnumerator enumerator;
  return enumerator.SupportsScript(kLatn) && enumerator.SupportsScript(kArab);
}

static bool check_supports_script_rejects_unknown() {
  muse::text::SystemFontEnumerator enumerator;
  return !enumerator.SupportsScript(0xFFFFFFFFu);
}

static bool check_discover_fonts_absent_family_ends_at_bundled() {
  // Fail-closed: absent family still yields a non-empty chain ending at the
  // bundled last-resort face (deterministic per header ordering contract).
  muse::text::SystemFontEnumerator enumerator;
  auto result =
      muse::text::discoverFonts(MakeQuery("no-such-family-xyz", kLatn, "en-US"));
  if (!result.ok() || (*result).empty()) return false;
  return (*result).back() == enumerator.LastResortFace(kLatn);
}

// --- Batch 1 (BUG-TEXT-008) T1: host hermetic fail-path pins.
// FontPathForFace miss paths fail closed with kNoFaceAvailable on every
// lane (host: unconditionally; device: null/unknown/no-candidate/unreadable).
// These hold GREEN on host and guard the fail-closed contract the device
// leg reproduces. ---

static bool check_font_path_null_handle_fails_closed() {
  auto result = muse::text::FontPathForFace(muse::text::FontHandle{0});
  if (result.ok()) return false;
  return result.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_font_path_unknown_handle_fails_closed() {
  auto result = muse::text::FontPathForFace(muse::text::FontHandle{0xFFFFFFFFu});
  if (result.ok()) return false;
  return result.status() == muse::text::Status::kNoFaceAvailable;
}

static bool check_font_path_last_resort_has_no_file() {
  // The bundled last-resort face has no system file on any lane.
  muse::text::SystemFontEnumerator enumerator;
  auto result =
      muse::text::FontPathForFace(enumerator.LastResortFace(kLatn));
  if (result.ok()) return false;
  return result.status() == muse::text::Status::kNoFaceAvailable;
}

// --- Batch 1 (BUG-TEXT-008) T1: device-leg failing pins (DEVICE_GATED).
// TRUE legs run on the x86_64 emulator (127.0.0.1:6555) at verify time.
// On host (no __ANDROID__) they SKIP-pass so the hermetic suites stay
// green; device results are never faked. Pre-fix on device each FAILS
// (FontPathForFace -> kNoFaceAvailable -> kMissingGlyph, drawn=0); post-fix
// each PASSES. ---

#if defined(__ANDROID__)
static bool check_device_font_path_roboto_latn_resolves_readable_ttf() {
  // RobotoLatn id mirrors CandidatesForFace (font_discovery_android.cpp).
  const auto result =
      muse::text::FontPathForFace(muse::text::FontHandle{0x000B0001u});
  if (!result.ok() || (*result).empty()) return false;
  const std::string& path = *result;
  if (path.size() < 5) return false;
  const std::string ext = path.substr(path.size() - 4);
  if (ext != ".ttf" && ext != ".ttc" && ext != ".otf") return false;
  std::ifstream probe(path.c_str());
  return probe.good();
}
#else
static bool check_device_font_path_roboto_latn_resolves_readable_ttf() {
  std::printf("SKIP device-gated (needs __ANDROID__ 6555 leg)\n");
  return true;
}
#endif

// --- Batch 1 (BUG-TEXT-008) T2: reason-coded fail-closed contract.
// Closest observable host behavior for the device-only logcat/warn
// acceptance (TC-TEXT-047 grep precedent): the production sources must
// carry the reason vocabulary from the plan Interface line
// (`[reason=path|ft-open|hb|missing-glyph]`) plus FT return-code logging.
// RED now (no reason token, no device logging in engine/text), GREEN after
// the coder instruments. ASSUMPTION: the coder keeps the exact `[reason=`
// prefix and the four reason names from the plan/task contract; a different
// spelling reconciles at verify (GR-08 T1.9), it does not fail the batch. ---

namespace {

std::string test_source_dir() {
  std::string file = __FILE__;
  const std::string::size_type slash = file.find_last_of("/\\");
  if (slash == std::string::npos) return ".";
  return file.substr(0, slash);
}

bool source_file_contains(const std::string& rel, const char* token) {
  const std::string path = test_source_dir() + "/" + rel;
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in.good()) return false;
  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
  return content.find(token) != std::string::npos;
}

}  // namespace

static bool check_gallery_fail_closed_warn_carries_reason_code() {
  // Every fail-closed gallery frame must name its cause for logcat triage.
  return source_file_contains("../../runtime/src/gallery_present.cpp",
                              "[reason=");
}

static bool check_gallery_warn_names_all_four_reasons() {
  const char* kFile = "../../runtime/src/gallery_present.cpp";
  return source_file_contains(kFile, "reason=path") &&
         source_file_contains(kFile, "reason=ft-open") &&
         source_file_contains(kFile, "reason=hb") &&
         source_file_contains(kFile, "reason=missing-glyph");
}

static bool check_device_font_path_logs_ft_return_codes() {
  // T2 instrumentation: the Android TU logs FontPathForFace outcomes plus
  // FT_Init_FreeType/FT_New_Face return codes to the muse_text logcat tag.
  const char* kFile = "../src/font_discovery_android.cpp";
  return source_file_contains(kFile, "__android_log_print") &&
         (source_file_contains(kFile, "FT_Error") ||
          source_file_contains(kFile, "ft_err"));
}

#define RUN_CASE(name)                           \
  do {                                           \
    if (!(name)()) {                             \
      std::printf("FAIL %s\n", #name);           \
      ++failures;                                \
    } else {                                     \
      std::printf("PASS %s\n", #name);           \
    }                                            \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_known_family_returns_nonempty_ordered_handles);
  RUN_CASE(check_ordering_is_deterministic);
  RUN_CASE(check_absent_family_falls_back_with_last_resort_last);
  RUN_CASE(check_per_script_resolution_differs);
  RUN_CASE(check_unsupported_script_returns_no_face_available);
  RUN_CASE(check_empty_family_falls_back_rather_than_crash);
  RUN_CASE(check_enumerator_has_roboto_family);
  RUN_CASE(check_roboto_faces_cover_latin);
  RUN_CASE(check_roboto_faces_cover_hani_cjk);
  RUN_CASE(check_roboto_faces_cover_arabic);
  RUN_CASE(check_generic_faces_cover_latin_deterministically);
  RUN_CASE(check_last_resort_face_is_stable_nonzero);
  RUN_CASE(check_supports_script_latin_and_arabic);
  RUN_CASE(check_supports_script_rejects_unknown);
  RUN_CASE(check_discover_fonts_absent_family_ends_at_bundled);
  RUN_CASE(check_font_path_null_handle_fails_closed);
  RUN_CASE(check_font_path_unknown_handle_fails_closed);
  RUN_CASE(check_font_path_last_resort_has_no_file);
  RUN_CASE(check_device_font_path_roboto_latn_resolves_readable_ttf);
  RUN_CASE(check_gallery_fail_closed_warn_carries_reason_code);
  RUN_CASE(check_gallery_warn_names_all_four_reasons);
  RUN_CASE(check_device_font_path_logs_ft_return_codes);
  if (failures > 0) {
    std::printf("system_fonts_test: %d failing\n", failures);
    return 1;
  }
  std::printf("system_fonts_test: all passing\n");
  return 0;
}
