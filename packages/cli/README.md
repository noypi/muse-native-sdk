# @muse/cli

Command-line interface for muse-native (`muse-native`, with deprecated
`muse` / `capo` aliases).

## `muse-native build <appDir>`

Builds the TSX app in `<appDir>` into two artefacts under `<appDir>/dist/`:

- `dist/ir.json` — syntax-neutral UI IR: `{ version: 1, roots: [...] }`
  with static-schema nodes (`{ type, props, slots, children }`) plus
  dynamic value slots (`{ kind: 'value' | 'prop', expression, ... }`).
- `dist/tree.mui.bin` — length-prefixed binary encoding of the same tree.

Both artefacts derive from the single lowered source tree (structural
lowering only; compiler legality is enforced by later lanes). The build
validates everything in memory before writing, so a failed build leaves
no partial `dist/` behind. Artefacts are always contained under
`<appDir>/dist/` — the build never writes outside the app directory.

### `tree.mui.bin` binary layout

All integers are unsigned 32-bit little-endian (u32LE):

| Offset | Size | Field |
| ------ | ---- | ----- |
| 0 | 4 | Magic header `MUI1` (ascii) |
| 4 | 4 | IR version (currently `1`) |
| 8 | 4 | Length-prefix `N`: payload length in bytes |
| 12 | N | Payload: utf8 JSON of the same `{ version, roots }` IR object |

Each record in the file is length-prefixed: the reader takes the magic,
checks the version, reads the `N` length prefix, then consumes exactly
`N` payload bytes. Decoders must reject truncated payloads, unknown
magic, and version mismatches.
