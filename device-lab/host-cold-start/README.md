# device-lab/host-cold-start — brownfield cold-start procedure (T9)

Scope: FR-PVIEWS-010, AC-PVIEWS-015, TC-PVIEWS-015 (external / device-lab).

## What this verifies

- The stock sample (`examples/brownfield-sample`) hosts EngineView in an
  existing Fragment with a single dependency addition.
- In shared-engine mode two hosts share one engine with isolated UI state
  (`hostsShareEngine: true`, `uiIsolated: true`).
- Measured host cold-start increase stays within the startup budget owned by
  perf (PERF-START — context only; this procedure reports
  `coldStartDeltaMs` vs `budgetMs`, it does not redefine the budget).

## Prerequisites (device-lab env)

- Emulator AVD per `device-lab/README.md` (API 29+ floor), `adb` attached.
- The brownfield sample app (coder work — absent, so this procedure
  currently fails closed; see Red state below).
- PERF-START budget value supplied as `budgetMs` inside the evidence JSON
  (owned by specs-performance.md: cold first frame 50–80 ms envelope —
  context, not asserted by this suite beyond delta-vs-budget).
- `HOST_COLD_START_EVIDENCE_PATH` optionally retargets the evidence path
  (path only; the verdict validates file content, never the env var).

## Running

```sh
device-lab/host-cold-start/run.sh          # verdict from evidence JSON
device-lab/host-cold-start/run.sh --help   # usage, exit 0
```

Exit codes: `0` verdict PASS; `1` missing/malformed evidence or any failing
check (budget overrun names `COLD_START_OVER_BUDGET { deltaMs, budgetMs }`);
`2` bad arguments (usage).

## Evidence seam (JSON)

Host-side path: `device-lab/host-cold-start/host-cold-start-result.json`
(on-device producer writes it; the runner validates the pulled file).
Minimal valid example:

```json
{
  "hostsShareEngine": true,
  "uiIsolated": true,
  "coldStartDeltaMs": 42,
  "budgetMs": 80
}
```

## Boundary / error cases

| Case | Expected |
|------|----------|
| any positional arg / unknown flag | exit 2 + usage |
| `--help` / `-h` | exit 0, no device touched |
| evidence file absent | exit 1, `FAIL — missing evidence at <path>` |
| malformed JSON / missing keys | exit 1, names invalid evidence |
| `hostsShareEngine != true` | exit 1, names `hostsShareEngine` |
| `uiIsolated != true` | exit 1, names `uiIsolated` |
| non-numeric `coldStartDeltaMs` / `budgetMs` | exit 1, invalid evidence |
| `coldStartDeltaMs > budgetMs` | exit 1, `COLD_START_OVER_BUDGET { deltaMs, budgetMs }` |
| `python3` absent on host | exit 1, names missing `python3` |
| `delta == budget` (exact boundary) | PASS (`<=` comparison) |

Host-side checks (arg validation, JSON shape, budget comparison) run without
a device; measurement itself requires device-lab — without evidence the
runner reports FAIL, never SKIP-as-pass.

## Red state (batch-first)

At contract time `device-lab/host-cold-start/` was absent (missing files).
This stub is the test file: until the sample + device measurement exist,
every invocation without evidence exits 1 (`missing evidence`), i.e. RED for
the expected reason (behavior missing, not test malformed).
