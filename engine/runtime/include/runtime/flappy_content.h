#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "runtime/renderer.h"

// Content module for the bundled sample game (FR-RUNTIME-013 / T5). This is
// the only place per-app tokens may appear: it registers the game's tags,
// tap-record gate, layout touchables, and scene-kind draw branches into the
// generic ContentRegistry. Generic dispatch files never include this header.
//
// Owned scene-block types (T5 unification): the evaluated-tree handle and
// per-frame stats live here — the single production definition. The generic
// present path reaches them only through TryPresentContentScene (declared in
// runtime/gallery_present.h); the hermetic contract suite binds the same
// symbols through this header. Never throws.
using TreeHandle = std::int32_t;  // opaque handle; production owns the type.
struct FrameStats {
  std::int32_t nodeCount = 0;
  std::int32_t unknownKindCount = 0;
  std::string unknownKindName;  // named kind when unknownKindCount > 0.
};

// Maps each evaluated game-scene node to its own draw path; returns the
// per-frame node count + unknown-kind count. Unknown kind > 0 fails closed
// with the kind named, never falls back to a gallery row. Never throws.
FrameStats renderTree(TreeHandle treeHandle);

// Registers an evaluated scene from presented content bytes (open family);
// null/empty input registers nothing (returns 0, no dereference).
// Never throws.
TreeHandle RegisterFlappyContentTree(const void* data, std::size_t len);

// Real DrawCmd emission feed (BUG-RUNTIME-009 / AC-RUNTIME-032): the
// production-owned command list staging for one frame. `renderTree` appends
// exactly one validated `runtime::DrawCmd` per emitted scene node and counts
// `nodeCount` only on appended commands; unknown kinds append nothing and
// return false with the kind named (RULE-RUNTIME-001). Never throws.
struct DrawCmdSink {
  std::vector<runtime::DrawCmd> cmds;
};

// Appends the validated `DrawCmd` for `kind`/`nodeId` to `sink` and returns
// true only when appended; returns false (no append) for unknown kinds,
// empty kinds, negative node ids, or degenerate bounds. Never throws.
bool DrawFlappySceneKindToSink(const std::string& kind, std::int32_t nodeId,
                               DrawCmdSink& sink);

// Number of `DrawCmd`s staged in `sink`. Never throws.
std::size_t EmittedDrawCmdCount(const DrawCmdSink& sink);

// Hermetic in-process feed accessor: returns the production-owned staging
// feed (no device, no GPU). Never throws.
DrawCmdSink& TestDrawCmdFeed();

namespace runtime {

// Drains the last `renderTree` frame's emitted `DrawCmd`s into `target` via
// `Swapchain::submit` (the real swapchain feed behind `Renderer::draw`).
// Returns the number of submitted commands. Never throws.
std::size_t FlushFlappyFrameToSwapchain(Swapchain& target);

// Registers the game content tags/handlers into the process-wide registry.
// Idempotent: repeat calls return true without replacing the first
// registration. Returns false only when a tag is already owned by a
// different bundle.
bool RegisterFlappyContent();

}  // namespace runtime
