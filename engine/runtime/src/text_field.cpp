// T5 production unit: focused-engine-row IME commit seam (Batch 3 T5,
// AC-RUNTIME-009/012, FR-RUNTIME-011). The IME commit write path stays the
// committed header-only `runtime::commit_text` (`runtime/text_field.h`); this
// TU owns the small focused-row registry that routes a commit to the engine
// row (never the hidden 1x1 EditTexts) and tracks the bound-view
// input-shown observable. VARIANCE V1: no `Utf8Bytes` type and no
// `mInputShown` field exist in engine/ — the seam uses only committed types
// (`NodeId`, `TextField`, `std::string_view`) and exposes the observable as
// `EngineRowInputShown()`. All entries are fail-closed and never throw
// (FR-RUNTIME-011 seam contract: no exception crosses the VM boundary).
#include "runtime/text_field.h"
#include "runtime/tree.h"

#include <cstddef>
#include <map>
#include <mutex>
#include <string>

namespace runtime {
namespace ime_commit {
namespace {

// Guards the registry below; the dispatch path is normally single-threaded,
// so this lock is uncontended there and exists to keep off-thread use safe.
std::mutex& CommitMu() {
  static std::mutex mu;
  return mu;
}

// Focused engine row content, keyed by committed `NodeId`. A row exists here
// only after `AttachEngineRow`/`SetFocusedEngineRow` (tap-claimed rows bind
// at focus time); unknown ids stay absent so commits to them fail closed.
std::map<NodeId, TextField>& RowStore() {
  static std::map<NodeId, TextField> rows;
  return rows;
}

// Currently focused engine row (`kInvalidNodeId` = no focus yet).
NodeId& FocusedRow() {
  static NodeId focused = kInvalidNodeId;
  return focused;
}

// Bound-view input-shown observable (the `mInputShown=true` half of the T5
// interface): set by a successful commit, never by seeding or by a
// failed/unknown commit.
bool& InputShown() {
  static bool shown = false;
  return shown;
}

// Commit path with the lock already held. Empty text is a no-op success that
// touches neither the row nor the observable (contract ASSUMPTION A2);
// unknown rows fail closed with no touch and no throw.
bool CommitLocked(NodeId rowId, const char* utf8, std::size_t len) {
  if (rowId == kInvalidNodeId) {
    return false;
  }
  if (len > 0 && utf8 == nullptr) {
    return false;
  }
  if (len == 0) {
    return true;
  }
  const auto it = RowStore().find(rowId);
  if (it == RowStore().end()) {
    return false;
  }
  commit_text(it->second, std::string_view(utf8, len));
  InputShown() = true;
  return true;
}

}  // namespace

// Bind an engine row (empty content) so later commits land in it. Unknown or
// invalid ids fail closed. Never throws.
void AttachEngineRow(NodeId row) noexcept {
  try {
    if (row == kInvalidNodeId) {
      return;
    }
    std::lock_guard<std::mutex> lock(CommitMu());
    RowStore().emplace(row, TextField{});
  } catch (...) {
  }
}

// Bind existing row content without touching the input-shown observable
// (load/focus-time binding; the observable is commit-owned only). Replaces
// the row content, caret collapsed to the end. Never throws.
void SetEngineRowText(NodeId row, const char* utf8, std::size_t len) noexcept {
  try {
    if (row == kInvalidNodeId) {
      return;
    }
    if (len > 0 && utf8 == nullptr) {
      return;
    }
    std::lock_guard<std::mutex> lock(CommitMu());
    TextField& field = RowStore()[row];
    field.text.assign(utf8 != nullptr ? utf8 : "", len);
    field.sel_a = field.text.size();
    field.sel_b = field.text.size();
    field.composition.clear();
    field.composition_caret = 0;
  } catch (...) {
  }
}

// Focus an engine row: records the focus and binds the row so later commits
// land in it (a tap-claimed row exists by construction). Never throws.
void SetFocusedEngineRow(NodeId row) noexcept {
  try {
    if (row == kInvalidNodeId) {
      return;
    }
    std::lock_guard<std::mutex> lock(CommitMu());
    RowStore().emplace(row, TextField{});
    FocusedRow() = row;
  } catch (...) {
  }
}

// Route an IME commit to one engine row (the T5 `commitTextToEngineRow`
// behavior on the committed seam): content lands via `commit_text` with the
// bound view reporting input-shown. Fail-closed, never throws.
bool CommitTextToEngineRow(NodeId rowId, const char* utf8,
                           std::size_t len) noexcept {
  try {
    std::lock_guard<std::mutex> lock(CommitMu());
    return CommitLocked(rowId, utf8, len);
  } catch (...) {
    return false;
  }
}

// Route an IME commit to the focused engine row. No focus (or an unfocused
// registry) fails closed with no touch and no throw.
bool CommitTextToFocusedEngineRow(const char* utf8,
                                  std::size_t len) noexcept {
  try {
    std::lock_guard<std::mutex> lock(CommitMu());
    if (FocusedRow() == kInvalidNodeId) {
      return false;
    }
    return CommitLocked(FocusedRow(), utf8, len);
  } catch (...) {
    return false;
  }
}

// Bound-view input-shown observable. Never throws.
bool EngineRowInputShown() noexcept {
  try {
    std::lock_guard<std::mutex> lock(CommitMu());
    return InputShown();
  } catch (...) {
    return false;
  }
}

// Focused engine row, or `kInvalidNodeId` when none. Never throws.
NodeId FocusedEngineRow() noexcept {
  try {
    std::lock_guard<std::mutex> lock(CommitMu());
    return FocusedRow();
  } catch (...) {
    return kInvalidNodeId;
  }
}

// Row content snapshot ("" for unknown rows). Never throws.
std::string EngineRowText(NodeId row) noexcept {
  try {
    std::lock_guard<std::mutex> lock(CommitMu());
    const auto it = RowStore().find(row);
    if (it == RowStore().end()) {
      return "";
    }
    return it->second.text;
  } catch (...) {
    return "";
  }
}

// Hermetic reset (rows, focus, observable). Never throws.
void ResetEngineRowCommitState() noexcept {
  try {
    std::lock_guard<std::mutex> lock(CommitMu());
    RowStore().clear();
    FocusedRow() = kInvalidNodeId;
    InputShown() = false;
  } catch (...) {
  }
}

}  // namespace ime_commit
}  // namespace runtime
