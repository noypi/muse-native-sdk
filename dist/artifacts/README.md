# Release artifacts (T10 / AC-BUILD-055)

Only `manifest.json` and this README are committed here; the release
binaries themselves stay untracked. Each retained entry and its
symbolication use:

- `release.aab` — shippable Play bundle; crashes against it are symbolicated with `mapping.txt` plus `unstripped/` native symbols.
- `universal-release.apk` — universal APK for sideload/lab installs; symbolicated the same way as `release.aab`.
- `mapping.txt` — R8/ProGuard mapping; de-obfuscates Java/Kotlin release stack traces.
- `unstripped/` — unstripped native libraries; symbolicate native crash frames (NDK `ndk-stack` / `addr2line`).
- `sourcemaps/` — JavaScript source maps; symbolicate Hermes/JS stack traces back to source.
- `sbom.json` — software bill of materials; maps build IDs to dependency versions for audit.
- `provenance.json` — SLSA build provenance; binds the artifacts above to the producing build (feeds the T11 provenance stage).
