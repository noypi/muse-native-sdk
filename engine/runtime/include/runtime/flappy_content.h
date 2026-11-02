#pragma once

// Content module for the bundled sample game (FR-RUNTIME-013 / T5). This is
// the only place per-app tokens may appear: it registers the game's tags,
// tap-record gate, layout touchables, and scene-kind draw branches into the
// generic ContentRegistry. Generic dispatch files never include this header.
namespace runtime {

// Registers the game content tags/handlers into the process-wide registry.
// Idempotent: repeat calls return true without replacing the first
// registration. Returns false only when a tag is already owned by a
// different bundle.
bool RegisterFlappyContent();

}  // namespace runtime
