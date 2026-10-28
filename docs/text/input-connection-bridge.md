# Thin InputConnection Bridge — Java/OS Boundary (FR-TEXT-009 / AC-TEXT-018)

Status: design note for manual verification (TC-TEXT-018). No callable
surface; the enforcement seam is `engine/runtime/include/runtime/alooper_bridge.h`.

## Rule

No business logic in Java. The Java/OS side is OS-boundary plumbing only:
event forwarding and lifecycle. All editing behavior — buffer, composition,
selection, queries — lives in C++.

## Layer diagram

```
┌─────────────────────────────┐
│ Android framework (OS-owned)│
│ InputConnection callbacks   │
└──────────────┬──────────────┘
               │ forward only
┌──────────────▼──────────────┐
│ Java boundary layer         │
│ (this note's whitelist)     │
└──────────────┬──────────────┘
               │ enqueue ImeEvent
┌──────────────▼──────────────┐
│ C++ ALooperBridge           │
│ alooper_bridge.h            │
│ queue → drain() → apply     │
└──────────────┬──────────────┘
               │ direct calls, zero JSI
┌──────────────▼──────────────┐
│ C++ GapBuffer +             │
│ composition_roundtrip.h     │
│ buffer / compose / select / │
│ surrounding-text            │
└─────────────────────────────┘
```

Events flow down; queries (`get_surrounding_text`, `get_composition_span`)
are answered synchronously on the same call stack — no deferred/async hop,
no JSI on the input path (`BridgeStats::jsi_touches` stays 0).

## Java-side whitelist (allowed)

- Forward `InputConnection` callbacks (`commitText`, `setComposingText`,
  `deleteSurroundingText`, `setSelection`, `getSurroundingText`) into
  `ImeEvent` posts. No interpretation of the text.
- Register/unregister the connection on lifecycle events (create/destroy,
  focus gain/loss). No state machine beyond "connected / disconnected".
- Pass through opaque spans/attributes without parsing them.

## Java-side ban list (business logic — must stay in C++)

- Buffer edits, cursor math, selection clamping, undo/redo.
- Composition-region tracking and commit conversion.
- Surrounding-text windowing and selection offsets.
- Queue sizing, drop policy, or retry logic (owned by `ALooperBridge`:
  `kMaxQueueDepth = 256`, drops counted in `BridgeStats::events_dropped`).

## C++-side ownership

| Concern | Owner |
|---|---|
| Byte buffer, cursors, undo/redo, span tags | `GapBuffer` (`runtime/gap_buffer.h`) |
| Event queue, drain, drop counting, zero-JSI guarantee | `ALooperBridge` (`runtime/alooper_bridge.h`) |
| Composing region, surrounding-text query | `composition_roundtrip.h` (T3 seam) |
| Verification of this boundary | `composition_roundtrip_test.cpp`, `ime_session_test.cpp` |

## Manual check (TC-TEXT-018 / AC-TEXT-018)

A reader opens this note and confirms: every Java file touching the input
path does only whitelist work above; anything resembling the ban list is a
violation to move into C++ behind the `ALooperBridge` seam.
