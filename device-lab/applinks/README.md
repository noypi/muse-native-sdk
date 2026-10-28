# device-lab/applinks — on-device verification procedure (T5, FR-NAV-006 AC-NAV-016)

Script: [`check_applinks.sh`](check_applinks.sh) · Dry-run harness:
[`tests/check_applinks_dryrun.sh`](tests/check_applinks_dryrun.sh) (stubbed `adb`, no device, no network).

This run ships the script + procedure only. The live-device pass (TC-NAV-016)
stays external/device-lab per plan A4 — no live-device pass is claimed here.

## Interface (contract)

```text
check_applinks.sh <package> <host> -> exit 0 VERIFIED | exit 1 NOT_VERIFIED | exit 2 DEVICE_ERROR
stdout: package=<p> host=<h> status=<verified|not_verified|error> detail=<pm output line>
```

What the script runs (API-gated, re-verify is failure-tolerant and never
changes the verdict):

```sh
adb shell pm get-app-links <package>
adb shell pm verify-app-links --re-verify <package>   # attempted, failure ignored
```

## Expected outputs (match the stdout contract exactly)

Verified host (exit 0):

```text
package=com.example.shop host=shop.example.com status=verified detail=com.example.shop: verified link handles for shop.example.com
```

Unverified host (exit 1, browser fallback — no hijack):

```text
package=com.example.shop host=shop.example.com status=not_verified detail=com.example.shop: unverified link handles for shop.example.com
```

Device error, e.g. `adb` missing or device offline (exit 2):

```text
package=com.example.shop host=shop.example.com status=error detail=error: device offline
```

Usage error (missing args) and `--help`:

```sh
check_applinks.sh            # exit 2, prints usage to stderr
check_applinks.sh --help     # exit 0, prints usage
```

## TC-NAV-016 mapping (AC-NAV-016 manual steps → lab execution)

| # | AC-NAV-016 manual step | Lab action (TC-NAV-016) | Expected `check_applinks.sh` result |
|---|------------------------|-------------------------|--------------------------------------|
| 1 | Verified link opens the app | Install the T2-generated APK (autoVerify intent-filters + matching `assetlinks.json`), run `check_applinks.sh <package> <host>`, then tap an `https://<host>/…` link | exit 0, `status=verified`; tap opens the app |
| 2 | Unverified host falls back to the browser | Same, but with a host missing from `assetlinks.json` (or before verification completes) | exit 1, `status=not_verified`; tap opens the browser, never the app |
| 3 | Malformed/hostile URI rejected, never navigates | Tap the T3 hostile set (wrong host, oversized param, percent-encoded `..` traversal, embedded `@` userinfo) — engine suites in `engine/navigation/tests/uri_validation_test.cpp` | No navigation occurs; `check_applinks.sh` verdict for the legitimate host is unchanged |
| 4 | Pre-install deferred link navigates after first launch | Click an ad/Play link before install, install, cold-start once — deferred capture→persist→replay (`engine/navigation/tests/deferred_link_test.cpp`, landing fallback for invalid links) | First launch navigates to the deferred destination; second launch does not replay |

## Dry-run (no device)

```sh
bash device-lab/applinks/tests/check_applinks_dryrun.sh   # expect: pass=6 fail=0, exit 0
bash -n device-lab/applinks/check_applinks.sh             # syntax check
```

The harness stubs `adb` on PATH with canned `pm` output (`verified` /
`not_verified` / `offline` modes) plus `--help`, usage-error, and
missing-`adb` cases. `shellcheck` (when available) must be clean.

## Recording the live-device result (external)

Append the `check_applinks.sh` stdout line(s) plus device model, OS build,
and date to the run record. A gate miss (e.g. `status=not_verified` where
`verified` is expected — usually an `assetlinks.json`/fingerprint drift, see
T2 `--check`) is a device-lab finding, not a unit-test failure: file it and
route to the planner.
