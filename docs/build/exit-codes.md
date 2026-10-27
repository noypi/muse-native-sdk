# `muse-native build` exit codes

Source of truth: `packages/cli/lib/exit-codes.js` (`getExitCode`).
These constants must match exactly — do not renumber without updating the code.

| Exit code | Failure class | Triggering example |
|-----------|---------------|--------------------|
| 0 | ok (success) | `muse-native build ./my-app` completes; all stages print `ok` and artefacts are written. |
| 2 | configuration / usage error | `muse-native build ./my-app --unknown-flag foo` (unknown option), or `--stage nope` (unknown stage), or a bad `--<key> <value>` type — fails at `configure`, before any compilation, with no `dist/` writes. |
| 1 | compile / stage error | `muse-native build ./my-app` where a pipeline stage fails (e.g. no `.tsx` sources, or a stage reports `FAIL`); also missing `<appDir>` or a non-existent app directory. |
| 3 | packaging error (reserved) | Reserved for the future packager (FR-BUILD owns it). The packaging path is currently a **stubbed mapping**: `getExitCode('package')` returns `3`, but no end-to-end packaging run exists yet. See repro 3 below. |

## Manual repro (TC-WORKFLOW-012)

Run each, then check `echo $?`:

1. Config error → expect exit `2`:
   `muse-native build ./my-app --unknown-flag foo; echo $?`
2. Compile / stage error → expect exit `1`:
   `muse-native build ./empty-app; echo $?`
   (Use an app directory whose stage fails, e.g. one with no `.tsx` sources.)
3. Packaging mapping → expect `3` (mapping-level only, not end-to-end):
   `node -e "console.log(require('./packages/cli/lib/exit-codes.js').getExitCode('package'))"`
   This asserts the reserved mapping. There is no live packager to fail end-to-end yet (per T6); do not report exit 3 as an observed packaging failure.
