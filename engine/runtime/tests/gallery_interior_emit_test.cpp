// Batch 1 RED pins — BUG-M3-FIDELITY-015 (FR-M3-007/AC-M3-015, FR-M3-011/AC-M3-025).
// Hermetic: C++17 + committed runtime headers only; in-memory rows driven
// through the retained entry `EmitGalleryWindowCmds`, no device, no network,
// no env/config reads, no filesystem, no live services.
// Mirrors gallery_text_emission_test.cpp / gallery_layering_test.cpp
// conventions (anonymous namespace, RUN_CASE, printf main).
//
// Binds to plan Batch 1 `Interface:` lines (see unit-tests/T1.md, T2.md):
//   T1: `EmitGalleryWindowCmds(rows, window, cmds) -> void`
//   T2: ownership — non-layering rows must flow through retained
//       `EmitGalleryWindowCmds`, never the `paintLayeringPage` slice.
//       `IsLayeringPageSlice` is forward-declared exactly as in
//       gallery_layering_test.cpp (defined in gallery_present.cpp, not in
//       the public header).
//
// EXPECTED RED STATE: the per-row interior cases (T1.3/T1.4, T2.9) fail with
// zero interior primitives while the chrome/caption guards stay green
// (theming intact). STOP rule: no TEXT-007 shaping files are touched here;
// shaping-dependent assertions (glyphRunId values, atlas reuse) are
// deliberately absent.
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"

namespace runtime {
bool IsLayeringPageSlice(const std::vector<GalleryRow>& rows, int first,
                         int last);
}  // namespace runtime

namespace {

constexpr runtime::PrimitiveKind kChromeKinds[] = {
    runtime::PrimitiveKind::ElevationShadow,
    runtime::PrimitiveKind::RoundedRect,
};

bool IsChrome(runtime::PrimitiveKind kind) {
  for (const auto k : kChromeKinds) {
    if (k == kind) return true;
  }
  return false;
}

// Caption strip kind (EmitCaptionCmd): never counted as an interior.
bool IsCaption(runtime::PrimitiveKind kind) {
  return kind == runtime::PrimitiveKind::Text;
}

// Interior = any retained primitive that is neither card chrome nor the
// caption strip, with non-degenerate bounds. Per AC-M3-015/AC-M3-025 every
// component row must carry at least one: the visible preview content.
bool IsInterior(const runtime::DrawCmd& cmd) {
  if (IsChrome(cmd.kind) || IsCaption(cmd.kind)) return false;
  return cmd.bounds.w > 0.0f && cmd.bounds.h > 0.0f;
}

// Gallery-list representative: ListItem rows (RowHeight/PaintComponent list
// extent; the m3-gallery list page component).
runtime::GalleryRow ListRow(const std::string& title) {
  runtime::GalleryRow row;
  row.comp = "ListItem";
  row.label = title;
  row.desc = "ListItem title=" + title;
  return row;
}

// Field-tab representative: TextInput rows (the muse-demo field-tab core
// component; FamilyKind TextInput branch).
runtime::GalleryRow FieldRow(const std::string& label) {
  runtime::GalleryRow row;
  row.comp = "TextInput";
  row.label = label;
  row.desc = "TextInput label=" + label;
  return row;
}

std::vector<runtime::GalleryRow> GalleryListRows() {
  return {ListRow("List item one"), ListRow("List item two"),
          ListRow("List item three")};
}

std::vector<runtime::GalleryRow> FieldTabRows() {
  return {FieldRow("Name"), FieldRow("Address")};
}

std::vector<runtime::DrawCmd> EmitWindow(
    const std::vector<runtime::GalleryRow>& rows) {
  runtime::RealizedWindow window;
  window.firstVisible = 0;
  window.lastVisible = static_cast<int>(rows.size()) - 1;
  window.prefetchAhead = 0;
  std::vector<runtime::DrawCmd> cmds;
  runtime::EmitGalleryWindowCmds(rows, window, cmds);
  return cmds;
}

std::vector<runtime::DrawCmd> CmdsForNode(
    const std::vector<runtime::DrawCmd>& cmds, std::uint32_t node) {
  std::vector<runtime::DrawCmd> out;
  for (const auto& cmd : cmds) {
    if (cmd.nodeId == node) out.push_back(cmd);
  }
  return out;
}

int InteriorCount(const std::vector<runtime::DrawCmd>& cmds) {
  int n = 0;
  for (const auto& cmd : cmds) {
    if (IsInterior(cmd)) ++n;
  }
  return n;
}

// Canonical 9-row layering scene (matches IsLayeringPageSlice kExpComp /
// kExpLabel order). Control: the predicate must stay true here so T2.7's
// false readings on list/field-tab rows are meaningful exoneration, not a
// dead predicate.
std::vector<runtime::GalleryRow> LayeringSceneRows() {
  static const char* kComp[9] = {"", "", "", "TextInput", "Dialog",
                                 "BottomSheet", "Menu", "SnackbarHost",
                                 "Tooltip"};
  static const char* kLabel[9] = {
      "Layering page", "Layering content dimmed light",
      "Layering content dimmed dark", "Layering IME probe", "Layering dialog",
      "BottomSheet", "Menu", "Layering snackbar", "Layering tooltip"};
  std::vector<runtime::GalleryRow> rows;
  for (int i = 0; i < 9; ++i) {
    runtime::GalleryRow row;
    row.comp = kComp[i];
    row.label = kLabel[i];
    row.desc = std::string(kComp[i]) + " " + kLabel[i];
    rows.push_back(row);
  }
  return rows;
}

// ---- T1: interiors missing from the emitted cmd stream ----

// T1 guard (theming intact): every component row keeps its card chrome.
bool check_t1_chrome_intact_per_row() {
  for (const auto& rows : {GalleryListRows(), FieldTabRows()}) {
    const auto cmds = EmitWindow(rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto node =
          CmdsForNode(cmds, static_cast<std::uint32_t>(i + 1));
      bool shadow = false, card = false;
      for (const auto& cmd : node) {
        if (cmd.kind == runtime::PrimitiveKind::ElevationShadow)
          shadow = true;
        if (cmd.kind == runtime::PrimitiveKind::RoundedRect) card = true;
      }
      if (!shadow || !card) {
        std::printf("  row %zu comp=%s: shadow=%d card=%d\n", i,
                    rows[i].comp.c_str(), (int)shadow, (int)card);
        return false;
      }
    }
  }
  return true;
}

// T1 guard (theming intact): every row keeps its caption strip.
bool check_t1_caption_intact_per_row() {
  for (const auto& rows : {GalleryListRows(), FieldTabRows()}) {
    const auto cmds = EmitWindow(rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto node =
          CmdsForNode(cmds, static_cast<std::uint32_t>(i + 1));
      bool caption = false;
      for (const auto& cmd : node) {
        if (IsCaption(cmd.kind)) caption = true;
      }
      if (!caption) {
        std::printf("  row %zu comp=%s: no caption Text cmd\n", i,
                    rows[i].comp.c_str());
        return false;
      }
    }
  }
  return true;
}

// T1 RED pin: every gallery-list / field-tab component row carries at
// least one interior primitive alongside chrome + caption.
bool check_t1_interior_present_per_row() {
  for (const auto& rows : {GalleryListRows(), FieldTabRows()}) {
    const auto cmds = EmitWindow(rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto node =
          CmdsForNode(cmds, static_cast<std::uint32_t>(i + 1));
      if (InteriorCount(node) == 0) {
        std::printf("  row %zu comp=%s: 0 interior primitives (%zu cmds)\n",
                    i, rows[i].comp.c_str(), node.size());
        return false;
      }
    }
  }
  return true;
}

// T1 RED pin (stream level): the retained window emission carries a
// nonzero interior population — never chrome + captions alone.
bool check_t1_interior_count_nonzero_stream() {
  std::vector<runtime::GalleryRow> rows = GalleryListRows();
  const auto field = FieldTabRows();
  rows.insert(rows.end(), field.begin(), field.end());
  const int n = InteriorCount(EmitWindow(rows));
  if (n == 0) {
    std::printf("  combined list+field-tab window: 0 interior primitives\n");
    return false;
  }
  return true;
}

// T1 boundary: empty rows emit nothing and never throw.
bool check_t1_empty_rows_emit_nothing() {
  const std::vector<runtime::GalleryRow> rows;
  return EmitWindow(rows).empty();
}

// T1 boundary: an empty realized window emits nothing and never throws.
bool check_t1_empty_window_emit_nothing() {
  const auto rows = GalleryListRows();
  runtime::RealizedWindow window;  // firstVisible=0, lastVisible=-1: empty
  std::vector<runtime::DrawCmd> cmds;
  runtime::EmitGalleryWindowCmds(rows, window, cmds);
  return cmds.empty();
}

// ---- T2: ownership — retained lane, not the layering slice ----

// T2 exoneration: list + field-tab rows are NOT a layering-page slice, so
// the layering early-return must not claim them.
bool check_t2_list_field_rows_are_not_layering_slice() {
  for (const auto& rows : {GalleryListRows(), FieldTabRows()}) {
    const int last = static_cast<int>(rows.size()) - 1;
    if (runtime::IsLayeringPageSlice(rows, 0, last)) {
      std::printf("  %s rows misclassified as layering slice\n",
                  rows[0].comp.c_str());
      return false;
    }
  }
  std::vector<runtime::GalleryRow> both = GalleryListRows();
  const auto field = FieldTabRows();
  both.insert(both.end(), field.begin(), field.end());
  if (runtime::IsLayeringPageSlice(both, 0,
                                   static_cast<int>(both.size()) - 1)) {
    std::printf("  combined rows misclassified as layering slice\n");
    return false;
  }
  return true;
}

// T2 control: the predicate still fires on the canonical layering scene,
// proving check_t2_list_field_rows_are_not_layering_slice is live.
bool check_t2_layering_slice_still_detected() {
  const auto rows = LayeringSceneRows();
  return runtime::IsLayeringPageSlice(rows, 0,
                                      static_cast<int>(rows.size()) - 1);
}

// T2 RED pin: interiors for list/field-tab rows flow through the retained
// `EmitGalleryWindowCmds` entry — this fails while the emission gap is
// open, and fails post-fix iff the fix lands in `paintLayeringPage`
// instead of the retained lane.
bool check_t2_retained_lane_carries_interiors() {
  for (const auto& rows : {GalleryListRows(), FieldTabRows()}) {
    const auto cmds = EmitWindow(rows);
    for (std::size_t i = 0; i < rows.size(); ++i) {
      const auto node =
          CmdsForNode(cmds, static_cast<std::uint32_t>(i + 1));
      if (InteriorCount(node) == 0) {
        std::printf("  row %zu comp=%s: retained lane emitted 0 interiors\n",
                    i, rows[i].comp.c_str());
        return false;
      }
    }
  }
  return true;
}

// T2 misplacement guard: the retained stream for list/field-tab rows must
// carry zero layering markers (OverlayScrim is paintLayeringPage-only).
// Green pre-fix; red iff interior content is routed via the layering slice.
bool check_t2_no_layering_markers_in_retained_stream() {
  for (const auto& rows : {GalleryListRows(), FieldTabRows()}) {
    for (const auto& cmd : EmitWindow(rows)) {
      if (cmd.kind == runtime::PrimitiveKind::OverlayScrim) {
        std::printf("  comp=%s: OverlayScrim in retained stream\n",
                    rows[0].comp.c_str());
        return false;
      }
    }
  }
  return true;
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
  RUN_CASE(check_t1_chrome_intact_per_row);
  RUN_CASE(check_t1_caption_intact_per_row);
  RUN_CASE(check_t1_interior_present_per_row);
  RUN_CASE(check_t1_interior_count_nonzero_stream);
  RUN_CASE(check_t1_empty_rows_emit_nothing);
  RUN_CASE(check_t1_empty_window_emit_nothing);
  RUN_CASE(check_t2_list_field_rows_are_not_layering_slice);
  RUN_CASE(check_t2_layering_slice_still_detected);
  RUN_CASE(check_t2_retained_lane_carries_interiors);
  RUN_CASE(check_t2_no_layering_markers_in_retained_stream);
  if (failures > 0) {
    std::printf("gallery_interior_emit_test: %d failing\n", failures);
    return 1;
  }
  std::printf("gallery_interior_emit_test: all passing\n");
  return 0;
}
