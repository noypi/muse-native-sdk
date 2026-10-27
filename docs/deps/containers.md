# Container choices for dependency maps (FR-DEPS-005)

## RULE-DEPS-006 — handle identity across mutation

**RULE-DEPS-006:** no pointer, reference, or iterator into a flat map may
outlive an insertion or erasure on that map. Identity that crosses a
mutation must be an integer handle (a key or index copied by value), never
a live reference into map storage. A value copy taken before the mutation
(`auto copy = map[id]; ... map.erase(id);`) satisfies the rule because no
reference survives; `auto &ref = map[k]; ... map.erase(k);` violates it
(AC-DEPS-011).

Rationale: flat maps (`ankerl::unordered_dense`,
`absl::flat_hash_map`) relocate entries on insert/erase, so any held
pointer/reference/iterator dangles. Integer handles stay valid because
they name the entry instead of pointing at its storage.

## Node-stable container choice (AC-DEPS-012)

When a map must erase entries while other entries stay referenced, do not
use a flat map — use a node-stable container whose references survive
erase of unrelated entries (for example `std::unordered_map` or
`std::map`). Decide the stability property at the declaration site:

```cpp
#include <string>
#include <unordered_map>

// Node-stable: erasing one session never moves the others, so handlers
// may keep `Session&` / iterators across `erase(id)`. A flat map would
// dangle here — identity crossing `erase` must otherwise be an integer
// handle (RULE-DEPS-006).
std::unordered_map<int, Session> sessions_by_id;
```

If the map is a flat map instead (`ankerl::unordered_dense`,
`absl::flat_hash_map`), the declaration site must state that no
reference outlives a mutation, and all cross-mutation identity uses
integer handles.

## Lint scope and limits (`tools/deps/flatmap_lint.py`)

- Scope: scans C++ sources (`.cpp`, `.cc`, `.cxx`, `.h`, `.hpp`, `.hh`)
  under `--root` for `auto&`/`Type&` references, `auto*` pointers, and
  `find`/`begin`/`end` iterators bound to an `ankerl::unordered_dense`
  or `absl::flat_hash_map` map and held across a later `insert`/`erase`
  (`erase`, `insert`, `emplace`, `try_emplace`, `erase_if`, `clear`,
  subscript store) on the same map. Violations print `file:line` with
  `AC-DEPS-011` and the `AC-DEPS-012` node-stable remedy.
- Limits: purely syntactic — one hold line before one mutation line on
  the same map variable in the same file. It does not track
  cross-function lifetimes, callback-held references, aliasing through
  wrappers, macro-generated access, or mutations hidden behind helpers.
  A clean lint pass does not prove RULE-DEPS-006 compliance; review any
  erasing map's stability choice by hand (TC-DEPS-012).
```

