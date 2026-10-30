// T5 — Gallery emission wiring: shape → cache → real ShapedText{glyphRunId}.
// FR-TEXT-016 / AC-TEXT-045 (shaped vector text from the engine stack, atlas
// reused across frames) + AC-TEXT-046 (CJK/emoji precondition, relocated
// CJK-first row-gate case from T4).
//
// Public boundary: `ParseGalleryRows` + `PresentGallery` from
// `runtime/gallery_present.h` (the plan T5 `Interface:` line's observable
// surface; see unit-tests/T5.md for the surface ASSUMPTION). Hermetic:
// C++17 + committed runtime headers only; in-memory MUI1 assets, an
// in-process FakeSwapchain, no device, no network, no config, no
// filesystem, no live services.
//
// Binds to the plan T5 `Interface:` line (see unit-tests/T5.md).
// RED STATE (expected): gallery emission still stuffs
// `payload.glyphRunId = node` (the row nodeId, never a shaping handle;
// gallery_present.cpp EmitFamilyCmd/EmitCaptionCmd) with no shaping call,
// no atlas, and no `measureLine` sizing; the ASCII-only first-character
// row gate (gallery_present.cpp ~581-585) drops every CJK/emoji-first
// label even after correct T4 decoding. So the reuse/CJK/emoji/sizing
// cases below fail while the fail-closed diagnostic guards already pass.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace {

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// In-process fake swapchain: records every submitted cmd. No GPU.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

std::vector<std::uint8_t> MakeAsset(const std::string& json) {
  std::vector<std::uint8_t> out;
  out.reserve(12 + json.size());
  out.push_back('M');
  out.push_back('U');
  out.push_back('I');
  out.push_back('1');
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  std::uint32_t n = static_cast<std::uint32_t>(json.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : json) out.push_back(static_cast<std::uint8_t>(c));
  return out;
}

std::string ShowcaseJson(const std::string& childrenJson) {
  return "{\"version\":1,\"roots\":["
         "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
         "\"props\":{},\"slots\":[],\"children\":[" +
         childrenJson + "]}]}";
}

std::string TextChild(const std::string& valueLiteral) {
  return "{\"type\":\"#text\",\"value\":\"" + valueLiteral + "\"}";
}

std::vector<std::string> LabelsFor(const std::string& valueLiteral,
                                   std::string* error) {
  std::vector<std::uint8_t> asset =
      MakeAsset(ShowcaseJson(TextChild(valueLiteral)));
  std::vector<std::string> labels;
  for (const auto& row : runtime::ParseGalleryRows(asset.data(), asset.size(),
                                                   error)) {
    labels.push_back(row.label);
  }
  return labels;
}

// Every cmd carrying a shaped run (ShapedText kind, or the Text caption
// cmd which carries the run reference in payload.glyphRunId).
std::vector<runtime::DrawCmd> ShapedCmds(const FakeSwapchain& target) {
  std::vector<runtime::DrawCmd> out;
  for (const auto& cmd : target.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText ||
        cmd.kind == runtime::PrimitiveKind::Text) {
      out.push_back(cmd);
    }
  }
  return out;
}

bool PresentLabels(const std::vector<std::string>& valueLiterals,
                   FakeSwapchain& target, std::string* error) {
  std::string children;
  for (std::size_t i = 0; i < valueLiterals.size(); ++i) {
    if (i > 0) children += ",";
    children += TextChild(valueLiterals[i]);
  }
  std::vector<std::uint8_t> asset = MakeAsset(ShowcaseJson(children));
  runtime::Renderer renderer(baseline_device());
  return runtime::PresentGallery(asset.data(), asset.size(), renderer, target,
                                 error);
}

// AC-TEXT-045 (atlas reused across frames): two rows with the identical
// label must reference the SAME cached run, not two per-node handles.
// Today each row is stuffed with its own nodeId, so the ids differ.
static bool check_identical_labels_share_one_run() {
  FakeSwapchain target;
  std::string error;
  if (!PresentLabels({"Primary action", "Primary action"}, target, &error)) {
    return false;
  }
  const auto shaped = ShapedCmds(target);
  if (shaped.size() < 2) return false;
  // Group run ids by emitting row (nodeId): both rows must resolve to one
  // shared run id from the cache.
  std::uint32_t first = 0;
  bool haveFirst = false;
  for (const auto& cmd : shaped) {
    if (cmd.payload.glyphRunId == 0) return false;
    if (!haveFirst) {
      first = cmd.payload.glyphRunId;
      haveFirst = true;
    } else if (cmd.nodeId != shaped[0].nodeId) {
      if (cmd.payload.glyphRunId != first) return false;
    }
  }
  // The two rows have distinct nodeIds; their run ids must still match.
  return shaped[0].nodeId != shaped.back().nodeId &&
         shaped[0].payload.glyphRunId == shaped.back().payload.glyphRunId;
}

// AC-TEXT-046 (relocated from T4): a CJK-first label must yield exactly 1
// row with exact UTF-8 bytes. Today the ASCII-only first-character gate
// drops the row even though T4 decodes it correctly.
static bool check_cjk_first_label_survives() {
  std::string error;
  const auto labels = LabelsFor("\\u4e2d\\u6587", &error);
  if (labels.size() != 1) return false;
  const std::string want("\xe4\xb8\xad\xe6\x96\x87", 6);
  return labels[0] == want;
}

// AC-TEXT-046: an emoji-first label must reach emission as a ShapedText cmd
// with a real nonzero run id. Today emoji-first rows are dropped by the row
// gate, so no shaped cmd exists for them.
static bool check_emoji_label_emits_shaped_text() {
  FakeSwapchain target;
  std::string error;
  if (!PresentLabels({"\\uD83D\\uDE00X"}, target, &error)) return false;
  const auto shaped = ShapedCmds(target);
  for (const auto& cmd : shaped) {
    if (cmd.payload.glyphRunId != 0) return true;
  }
  return false;
}

// AC-TEXT-045 (measureLine sizing replaces TextWidth byte-counting):
// shaped bounds must be deterministic reuse, not per-node slots — the same
// label twice yields identical run id AND identical bounds. Today the run
// ids are per-node, so this fails on the id half.
static bool check_identical_labels_share_bounds_and_run() {
  FakeSwapchain target;
  std::string error;
  if (!PresentLabels({"Primary action", "Primary action"}, target, &error)) {
    return false;
  }
  const auto shaped = ShapedCmds(target);
  if (shaped.size() < 2) return false;
  const auto& a = shaped[0];
  const auto& b = shaped.back();
  if (a.nodeId == b.nodeId) return false;
  return a.payload.glyphRunId == b.payload.glyphRunId &&
         a.bounds.w == b.bounds.w && a.bounds.h == b.bounds.h &&
         a.bounds.w > 0.0f && a.bounds.h > 0.0f;
}

// AC-TEXT-045 (Latin baseline guard): a Latin label emits at least one
// shaped cmd with a nonzero run id. Passes today via the nodeId
// placeholder; stays green when the id becomes a real shaping handle.
static bool check_latin_label_emits_shaped_text_nonzero() {
  FakeSwapchain target;
  std::string error;
  if (!PresentLabels({"Primary action"}, target, &error)) return false;
  const auto shaped = ShapedCmds(target);
  if (shaped.empty()) return false;
  for (const auto& cmd : shaped) {
    if (cmd.payload.glyphRunId == 0) return false;
  }
  return true;
}

// RULE-RUNTIME-001 fail-closed guard: a malformed escape drops its row and
// no emitted label ever contains the legacy '?' substitution. Already
// fail-closed in-tree; stays green.
static bool check_malformed_escape_never_question_mark() {
  std::string error;
  const auto labels = LabelsFor("A\\uD83DX", &error);
  for (const auto& l : labels) {
    if (l.find('?') != std::string::npos) return false;
  }
  return labels.empty();
}

// RULE-RUNTIME-001 fail-closed guard: a bad envelope emits a diagnostic
// and records nothing (never a gallery bitmap). Already fail-closed
// in-tree; stays green.
static bool check_present_gallery_diagnostic_on_bad_envelope() {
  FakeSwapchain target;
  std::string error;
  const char bad[] = "NOPE-not-mui1-payload";
  runtime::Renderer renderer(baseline_device());
  const bool ok = runtime::PresentGallery(bad, sizeof(bad) - 1, renderer,
                                          target, &error);
  return !ok && !error.empty() && target.captured.empty();
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_identical_labels_share_one_run);
  RUN_CASE(check_cjk_first_label_survives);
  RUN_CASE(check_emoji_label_emits_shaped_text);
  RUN_CASE(check_identical_labels_share_bounds_and_run);
  RUN_CASE(check_latin_label_emits_shaped_text_nonzero);
  RUN_CASE(check_malformed_escape_never_question_mark);
  RUN_CASE(check_present_gallery_diagnostic_on_bad_envelope);
  if (failures > 0) {
    std::printf("gallery_text_emission_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_text_emission_test: all passing\n");
  return 0;
}
