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
>
> Update 2026-11-01 (T7 device session, commit `e10483b`): a single-variant
> debug APK was built from this commit, installed on device 127.0.0.1:6562,
> and verified launched/rendered (§5). The repo contains NO Cronet AAR or
> prebuilt native library — only the `cronet_transport.cc` plug shim — so a
> with/without-Cronet APK A/B remains INFEASIBLE: there is no second variant
> to build. §5 numbers are MEASURED single-variant; the delta stays OPEN.

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

## 5. Device evidence — single-variant debug APK (MEASURED, 2026-11-01)

Built from commit `e10483b699eac4d7e7a20979f13bd09f36006dfe`
(branch `run/fr-deps-009-fr-net-014-nfr-build-020-fr-media-013`):

- Engine: `make engine-android` (NDK 27.2.12479018, platform android-29),
  4 ABIs. `libRuntime.so` bytes: arm64-v8a 6352864, armeabi-v7a 5608148,
  x86_64 6078152, x86 5911640.
- SDK stamp: `sh scripts/release-sdk.sh --allow-dirty` → `build/VERSION`
  `0.1.0` (matches `sdk: 0.1.0` pin in `examples/m3-gallery/muse-native.yaml`).
- Example: `make clean && make apk && make sign` in `examples/m3-gallery`
  (`MUSE_NATIVE_HOME=<run>/build`,
  `ANDROID_PLATFORM=/media/saif/EXTRA/sdk/android/platforms/android-35`).
- APK (`examples/m3-gallery/build/m3-gallery-debug.apk`, debug key only):
  **6341553 bytes**, sha256
  `4e5db3a5e6ef9d57b232eb2efa0f41b38c609e2c998c7f11b28840c7b993b976`.
  Unsigned 6334434, aligned 6334436. Uncompressed native payload
  (4 × `libRuntime.so`): 24035837 bytes total per `unzip -l`
  (APK deflates the `.so` entries ~73–75%).
- Install: `adb -s 127.0.0.1:6562 install -r build/m3-gallery-debug.apk`
  → `Success` (note: `make install` without `ANDROID_SERIAL` fails with
  "more than one device/emulator" — 3 devices on this adb server; use
  `-s 127.0.0.1:6562`). Device `pm path com.muse.host` →
  `/data/app/com.muse.host-7gW2nfKFkmLqBJiiXgq5mw==/base.apk`,
  `du -k` → **6200 KB on-device** (≈ 6348800 bytes incl. filesystem blocks).
- Device: model `Phone` (Genymobile vbox86p), **Android API 29
  (Android 10)**, serial `127.0.0.1:6562` (`adb devices` state `device`).
- Launch proof: `am start -n com.muse.host/.MainActivity` → pid 8362,
  `mResumedActivity … com.muse.host/.MainActivity`,
  logcat `Displayed com.muse.host/.MainActivity: +702ms`,
  `muse-host: SEAM-STEP:render:ok`, `GalleryPage: gallery-rows=121`.
  Screenshot: `docs/build/t7_install_proof.png` (this dir).
- Cronet A/B feasibility: NO Cronet AAR/prebuilt exists anywhere in this
  commit (`find . -iname '*cronet*'` outside `build/` returns only
  `engine/net/{include,src}` shim sources + host test binaries). The
  engine links no Cronet native library, so a with/without-Cronet APK pair
  cannot be produced from this tree — the T7 A/B delta stays OPEN pending
  vendoring a Cronet AAR and a release device-lab build.

## 6. Gate status (Q-NET-001)

- Both-binary APK numbers: NOT BUILT (infeasible this session, §1). Proxy
  pair built from the same commit: YES (§2).
- Per-app delta vs perf estimates: RECORDED as open/estimated (§3), not as
  measured APK bytes.
- Filed-before-default-ships: REVIEWER TO CONFIRM. Note: `DefaultTransportName()`
  in `engine/net/include/net/transport_config.h` (T6) already returns
  `"cronet"` in code; no template ships Cronet as default until the reviewer
  signs this record. Q-NET-001 stays open until that sign-off plus the
  device-lab A/B numbers.
