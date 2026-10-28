# State API Surface (FR-STATE-001 / FR-STATE-002 / FR-STATE-006)

Scope: T4, AC-STATE-009. Docs-only; no callable surface.

## 1. Module surface ownership (NFR-STATE-005)

The ESM module surface of this subsystem is owned by core. `packages/core`
re-exports the public state API (`state`, store, inset bus, registry); this
subsystem supplies the underlying C++ primitives consumed through that
core-owned surface and adds no parallel public entry point.

## 2. Allocation honesty boundary (AC-STATE-009)

The C++ render path is allocation-stable and GC-free after bootstrap: steady-state
frame work performs no heap allocation on the hot path and therefore triggers no
collection. The Hermes / JS isolate is outside that path and still collects
garbage normally; every JS-authored callback, action body, and subscriber runs
under the collector and may allocate.

## 3. Wording rule (PERF-HONEST)

PROHIBITION: Never describe JS-authored logic as "zero GC" (see PERF-HONEST).
