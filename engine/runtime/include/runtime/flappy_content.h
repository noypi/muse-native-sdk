#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

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

namespace runtime {

// Registers the game content tags/handlers into the process-wide registry.
// Idempotent: repeat calls return true without replacing the first
// registration. Returns false only when a tag is already owned by a
// different bundle.
bool RegisterFlappyContent();

}  // namespace runtime
