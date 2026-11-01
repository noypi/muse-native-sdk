# Third-party sources

Third-party sources live in the shared workspace's `third-party` folder.

- Treat shared workspace's `third-party/` folder as the single source of truth for all
  vendored / mirrored third-party code and tarballs.
- Do not vendor third-party code anywhere else; add new third-party sources
  under shared workspace's `third-party/` folder only.
- See shared workspace's `third-party/README.md` (and `SHA256SUMS` beside it)
  for the vendoring policy, verification steps, and per-dependency versions.
