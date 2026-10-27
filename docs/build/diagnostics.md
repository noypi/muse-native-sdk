# Build diagnostics

## Literal style values and theme-awareness

A literal style value does not follow a light or dark switch or a Dynamic Color change; use a token reference for theme-aware styling.

Literal colour, dimension and other style values in application code compile
with no diagnostic. A literal never tracks the active theme: it does not
follow a light/dark switch and it does not update on a Dynamic Color change.
For theme-aware styling, use a token reference instead of a literal value.

## CAPO0012 — literal duplicates an M3 role-token value (advisory)

When a literal style value equals a value already carried by an M3 role token,
the build emits the advisory diagnostic `CAPO0012` naming the literal, its
source location (file, line, column, node path) and the matching token. The
diagnostic severity is `advisory` and the build still exits zero.
