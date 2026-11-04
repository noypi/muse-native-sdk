# third-party/ — vendored dependency record (committed to the repo)

This directory is the committed RECORD of what is vendored, not the vendored
sources themselves: the tarballs and extracted sources live OUTSIDE the repo at
the shared workspace path (see `Vendored location` below) and are resolved
exclusively via `MUSE_NATIVE_THIRD_PARTY_DIR`. Keep this record and that
location in sync — a mismatch fails closed.

# ... original vendor ledger follows (shared-workspace copy) ...

# third-party/ — vendored dependency sources (shared workspace copy)

Policy (architect decision 2026-11-01 — proactive vendoring, not gated on a failing build):
this folder lives in the shared workspace, NOT in the repo. No build step may assume a
repo-relative path to it — resolve it exclusively via `MUSE_NATIVE_THIRD_PARTY_DIR`
(see `NFR-BUILD-021` / `RULE-BUILD-003` in `manual-context.local/specs-build-toolchain.md`).

- Each tarball below was integrity-verified at fetch time (`gzip -t` / `xz -t`) and its
  SHA-256 recorded in `SHA256SUMS` next to it. Re-verify with `sha256sum -c SHA256SUMS`
  before use. A mismatch fails closed — re-download, never proceed.
- No network at build time: the tarballs ARE the download. Extraction is per-dependency,
  on demand, into the build tree — never in-source, never the whole folder at once.
- Licence texts ship in build notices at link time per `specs-cpp-dependencies.md`
  (FR-DEPS-006). FFmpeg (LGPL) additionally needs its source offer + relinkable objects —
  its entry below is a pin only; do NOT vendor FFmpeg casually.
- Never-vendored (policy, not omission): AAudio, Media3, Oboe (NDK/Play), Play Core NDK
  (Play), WebRTC (dynamic feature module only), Vulkan-Hpp (Vulkan SDK/NDK),
  Yoga/Cassowary (admitted references only — in-tree layout code is the implementation).

## Vendored (verified 2026-11-01)

| Name | Version | Upstream | File | Size | SHA-256 (see SHA256SUMS) | Licence | Link |
|------|---------|----------|------|------|---------------------------|---------|------|
| concurrentqueue | v1.0.5 | cameron314/concurrentqueue | concurrentqueue-v1.0.5.tar.gz | 1.5M | 4d6368a2…373a6ef3 | MIT | static (header-only) |
| unordered_dense | v5.3.3 | martinus/unordered_dense | unordered_dense-v5.3.3.tar.gz | 2.5M | 862605ef…83228eca64 | MIT | static (header-only) |
| magic_enum | v0.9.8 | Neargye/magic_enum | magic_enum-v0.9.8.tar.gz | 204K | 1e54959a…7232da2b0 | MIT | static (header-only) |
| fmt | 12.2.0 | fmtlib/fmt | fmt-12.2.0.tar.gz | 724K | 8b852bb5…38dfd3a177 | MIT | static |
| simdjson | v5.1.1 | simdjson/simdjson | simdjson-v5.1.1.tar.gz | 8.5M | d669fa39…063dbf97f3e6 | Apache-2.0 | static (tooling only) |
| vk-bootstrap | v1.4.366 | charles-lunarg/vk-bootstrap | vk-bootstrap-v1.4.366.tar.gz | 220K | d71f6ef6…f63cf6b4e751 | MIT | static |
| VMA | v3.5.0 | GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | vma-v3.5.0.tar.gz | 980K | c5f3db84…0902e66474e9e3 | MIT | static (header-only) |
| QuickJS | 2026-06-04 | bellard.org/quickjs | quickjs-2026-06-04.tar.xz | 608K | b376e839…19ea2fa70ad2a | MIT | static |
| FlatBuffers | v25.12.19-2026-02-06-03fffb2 | google/flatbuffers | flatbuffers-v25.12.19.tar.gz | 2.3M | ccbce586…106fe50aace0 | Apache-2.0 | static |
| FreeType | 2.14.1 | download.savannah.gnu.org | freetype-2.14.1.tar.xz | 2.6M | 32427e8c…48230bab3df2936 | FTL | static (superseded by 2.15.0 below) |
| FreeType | 2.15.0 | download.savannah.gnu.org | freetype-2.15.0.tar.gz | 4.3M | 918662…a8ba8d | FTL | static (current per 2026-11-02 owner approval) |
| ICU4C | 79.1 | unicode-org/icu | icu4c-79.1-sources.tgz | 28M | 06fa63ec…bc94902b | Unicode-DFS-2016 | static (vendored 2026-11-02 for text-engine link; replaces deferred pin) |
| HarfBuzz | 15.0.0 | harfbuzz/harfbuzz | harfbuzz-15.0.0.tar.xz | 20M | 968536e9…221ec42d2a3be4a44df | MIT | static |
| Skia | chrome/m158 @ 232c4710 (2026-11-03) | google/skia | skia-chrome-m158.tar.gz | 76M | 24af102e…32f5a1e3a | BSD-3-Clause | static |

Total: ~148MB (was ~72MB before the Skia m158 snapshot, added 2026-11-04 for the Ganesh+GLES quick-landing bridge; ~40MB before ICU 79.1 + FreeType 2.15.0, added 2026-11-02 for the text-engine link). Versions were resolved against upstream metadata at fetch time (GitHub
releases API, `git ls-remote` where the API had no release object, bellard.org index,
Savannah mirror probe) — not from memory. Corrections found that way: VMA lives at
GPUOpen-LibrariesAndSDKs (the old GPUOpen/VulkanMemoryAllocator path is gone), QuickJS
2026-06-04 supersedes the remembered 2024-02-14, FreeType 2.14.1 confirmed (2.13.3 fallback
unneeded).

## Pinned, not vendored (deferred with reason)

| Name | Pin | Reason deferred |
|------|-----|-----------------|
| Hermes | needs version + toolchain decision | 100MB+, own build system; QuickJS covers the low-memory path meanwhile |
| Crashpad | needs version + depot_tools decision | huge; tombstones suffice until crash reporting is scheduled |
| Lottie / Rive | importer versions TBD at motion-lane scheduling | importers only; no frame-path consumer yet |
| libswappy | archived upstream (google/swappy) | gated on the EGL present-path decision |
| FFmpeg | pinned in manifest (`--enable-shared --disable-shared --disable-gpl --disable-nonfree`) | LGPL: licence text + source offer + relinkable objects must land together — deliberate compliance change, not a download |
| Vulkan-Hpp | arrives with the Vulkan SDK/NDK | never vendored |
| Yoga | admitted reference only | in-tree layout code is the implementation; nothing to fetch |
| Cassowary | admitted reference only | same as Yoga |
| WebRTC / Media3 / AAudio / Oboe / Play Core NDK | system / Play / dynamic | never vendored by policy |
