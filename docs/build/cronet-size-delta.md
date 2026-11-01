# Cronet size-delta vs perf footprint estimates (TC-NET-028 record)

T7 record for FR-NET-014 / AC-NET-028. Q-NET-001 gate evidence: this file is
the filed delta record; the reviewer confirms it before Cronet ships as the
default in any template (blocks T6 default shipment while the gate is open).

> Honesty note: the full two-binary Cronet A/B APK measurement was
> attempted and is INFEASIBLE in this session (no Android release build, no
> Cronet AAR, no device-lab device). No APK byte counts below are presented
> as measured. What IS measured is the host-proxy A/B pair in §2. The
> per-app APK delta table in §3 is ESTIMATED / pending device-lab
> measurement, computed against the NFR-BUILD-019 estimates by reference.

## 1. Method attempted

1. Goal (TC-NET-028): build the reference binary WITH and WITHOUT Cronet
   from the same commit, record the per-app size delta against the perf
   footprint estimates.
2. Attempted: full release A/B (`dist/app-release.aab` with/without the
   Cronet native library). NOT POSSIBLE here — the repo has no release AAB
   artifact, no Cronet prebuilt/AAR vendored, and no device-lab device in
   this session. No APK numbers are claimed.
3. Substituted (measured): host-proxy A/B from the same commit
   (`5f3e138`, batch 2 base; working tree carries staged batch-3 net
   changes including `engine/net/src/cronet_transport.cc`):
   - `g++ -std=c++17 -O2 -I engine/net/include -c engine/net/src/cronet_transport.cc`
   - `with_cronet`: harness + `cronet_transport.cc`; `baseline`: harness only.
   - Toolchain: `g++ (Debian 14.2.0-19)`, x86-64 host, `-O2`, unstripped.
   - This measures the in-repo Cronet plug shim only — NOT the real Cronet
     native library, which dominates any shipping APK delta. Do not
     extrapolate these bytes to APK size.

## 2. Measured (host proxy, same commit)

| Artifact | Bytes | Status |
|----------|-------|--------|
| `baseline` (harness, no Cronet TU) | 15952 | MEASURED |
| `with_cronet` (harness + `cronet_transport.cc`) | 17744 | MEASURED |
| Proxy delta (with − baseline) | +1792 (+11.2% of baseline) | MEASURED |
| `cronet_transport.o` (`text 1264 + data 96`, file 6976) | 6976 file / 1360 sections | MEASURED |

Scope of "measured": host x86-64 proxy only. The shipping Cronet cost is the
Cronet AAR native `.so` (arm64-v8a), which this proxy does not contain.

## 3. Per-app delta vs NFR-BUILD-019 estimates

Estimates by reference (never redefined here): NFR-BUILD-019 consumes
`binary_and_footprint_estimates` from `specs-performance.md`
(NFR-PERFORMANCE-006, AC-PERFORMANCE-009): base compressed APK 12–18 MB,
ICU subset ~3 MB, dynamic modules 15–25 MB, full-stack installed 35–50 MB.

| App / artifact | NFR-BUILD-019 estimate (ref) | Cronet APK delta | Status |
|----------------|------------------------------|------------------|--------|
| Base compressed APK (12–18 MB class) | 12–18 MB compressed | NOT MEASURED — pending device-lab release A/B | ESTIMATED / OPEN |
| Per-split APK (arm64-v8a etc.) | per-artifact budget via `toolchain/size` gates vs perf manifest | NOT MEASURED — pending device-lab release A/B | ESTIMATED / OPEN |
| Full-stack installed (35–50 MB) | 35–50 MB installed | NOT MEASURED — pending device-lab release A/B | ESTIMATED / OPEN |

Follow-up to close the estimates: run the device-lab release A/B
(with/without Cronet AAR from the same commit), record per-artifact
`downloadBytes`/`installBytes` in `docs/build/size-trends.md`, and let the
`toolchain/size` gates compare them against the perf budgets.

## 4. Gate status (Q-NET-001)

- Both-binary APK numbers: NOT BUILT (infeasible this session, §1). Proxy
  pair built from the same commit: YES (§2).
- Per-app delta vs perf estimates: RECORDED as open/estimated (§3), not as
  measured APK bytes.
- Filed-before-default-ships: REVIEWER TO CONFIRM. Note: `DefaultTransportName()`
  in `engine/net/include/net/transport_config.h` (T6) already returns
  `"cronet"` in code; no template ships Cronet as default until the reviewer
  signs this record. Q-NET-001 stays open until that sign-off plus the
  device-lab A/B numbers.
