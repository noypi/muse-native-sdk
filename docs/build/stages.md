# Build stages

Seven stages, in order: `configure`, `typecheck`, `compile`, `emit`, `native-build`, `package`, `install` (see `packages/cli/lib/stages.js`).

Running `muse-native build <appDir>` runs all seven in order, stopping at the first failure. Each stage reports `<stage>: ok (<ms>ms)` or `<stage>: FAIL <first-error> at <location>`.

Only `emit` and `package` write artefacts (`dist/ir.json` and `dist/tree.mui.bin` respectively). Stages are fail-closed: everything is validated/computed in memory before anything is written, so a failure leaves `dist/` untouched.

Toolchain behavior owned by FR-BUILD-* (what an option does once resolved, NDK/R8/packaging/install mechanics) is deliberately out of scope here; where a stage is currently a structural gate, that is stated honestly below.

## configure

- Inputs: `appDir` (must be an existing directory), resolved config, `.tsx` sources under `appDir`.
- Outputs: no artefacts, no in-memory tree. Passes when `appDir` is a directory and at least one `.tsx` source can be read; otherwise fails naming `appDir` as the location.
- Single-stage command: `muse-native build --stage configure <appDir>`

## typecheck

- Inputs: `appDir`, resolved config, `.tsx` sources under `appDir`.
- Outputs: no artefacts, no in-memory tree. Currently a structural gate: passes when the sources load (same closed gate as `configure` — fails with `no .tsx sources found in '<appDir>'` when empty). A dedicated typechecker, if any, belongs to a later run (FR-BUILD-*); this stage does not claim to run one today.
- Single-stage command: `muse-native build --stage typecheck <appDir>`

## compile

- Inputs: `appDir`, resolved config, `.tsx` sources under `appDir`.
- Outputs: no `dist/` writes. Lowers sources to the in-memory IR tree (`ctx._tree`) and validates the tree is non-empty and encodable; on lowering/encoding failure reports `cannot build '<appDir>': <detail>` at `<appDir>`.
- Single-stage command: `muse-native build --stage compile <appDir>`

## emit

- Inputs: `appDir`, resolved config, `.tsx` sources under `appDir`.
- Outputs: writes `dist/ir.json` (pretty-printed IR tree) and records the in-memory tree. Fails closed: lowering/encoding is validated in memory first; a write failure removes the partial `ir.json` and reports `cannot write artefacts for '<appDir>': <detail>` at the `ir.json` path.
- Single-stage command: `muse-native build --stage emit <appDir>`

## native-build

- Inputs: `appDir`, resolved config.
- Outputs: no artefacts. Currently a structural no-op gate: passes when `appDir` is a directory. Native toolchain invocation (NDK/16 KB/Baseline) is owned by FR-BUILD-* and is not implemented in this run; this section will describe its real inputs/outputs when that run lands.
- Single-stage command: `muse-native build --stage native-build <appDir>`

## package

- Inputs: `appDir`, resolved config, `.tsx` sources under `appDir`.
- Outputs: writes `dist/tree.mui.bin` (encoded IR blob). Fails closed: lowering/encoding is validated in memory first (empty tree/blob rejected); a write failure removes the partial `tree.mui.bin` and reports `cannot write artefacts for '<appDir>': <detail>` at the `tree.mui.bin` path.
- Single-stage command: `muse-native build --stage package <appDir>`

## install

- Inputs: `appDir`, resolved config.
- Outputs: no artefacts. Currently a structural no-op gate: passes when `appDir` is a directory. Device/install mechanics belong to a later run; this section will describe its real inputs/outputs when that run lands.
- Single-stage command: `muse-native build --stage install <appDir>`
