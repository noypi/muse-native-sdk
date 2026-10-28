# GC Model

## Scope

This document states the garbage-collection model for the render path and
the JS isolate. It is a docs-only statement and exposes no callable surface.

## Render path

The C++ render path performs no garbage collection. Frame rendering uses
explicit lifetime management (RAII / arenas) and runs with GC-free
deterministic reclamation, so no collector pauses can originate from the
render path itself.

## JS isolate

The JS isolate still collects. JavaScript-authored logic executes in a
garbage-collected heap, and the isolate's collector continues to run and
reclaim JS objects as before.

## Prohibition

This document makes no process-wide zero-GC claim.

Do not describe the process as a whole as collector-free: the GC-free
guarantee covers only the C++ render path, never the JS isolate. No
broader zero-collector claim about the process must be made.
