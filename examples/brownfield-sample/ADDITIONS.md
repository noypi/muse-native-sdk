# brownfield-sample — coder additions map (T9)

Frozen contract: `README.md` (do not edit). This file maps each README
requirement to the scaffold file that satisfies it.

| README requirement | File |
|---|---|
| Single dependency addition | `build.gradle` (`implementation "com.muse.pviews:pviews:1.0.0"` only) |
| Manifest-clean (no entries beyond engine's own) | `AndroidManifest.xml` (only pre-existing `HostActivity`) |
| `HostActivity` (existing) + `ExistingFragment` with `EngineView` | `src/.../HostActivity.kt`, `ExistingFragment.kt`, `res/layout/activity_host.xml`, `res/layout/fragment_existing.xml` |
| Second host, same engine, isolated UI (shared mode MUST) | `src/.../SecondFragment.kt`, `res/layout/fragment_second.xml`, `SharedEngineHolder.kt` (`contentDescription` host-a vs host-b) |
| Multi-engine second screen (MAY) | `src/.../MultiEngineFragment.kt`, `res/layout/fragment_multi_engine.xml` |
| Engine init failure → host-owned fallback (T7) | `fragment_existing.xml` (`@id/engine_fallback`) + `ExistingFragment.kt` fallback branch + `SharedEngineHolder.simulateInitFailure` |
| Evidence `{hostsShareEngine, uiIsolated}` source | `SharedEngineHolder.snapshot()` → consumed by `device-lab/host-cold-start/measure.sh` on device |

Device run (requires emulator AVD per `device-lab/README.md`, API 29+):
`device-lab/host-cold-start/measure.sh` writes the pulled JSON;
`device-lab/host-cold-start/run.sh` validates it (fail closed, no evidence ⇒ exit 1).
