# Device-lab gate — storage permissions (TC-NET-025)

External gate for FR-NET-012 / AC-NET-025. This directory carries the
procedure note only; the hermetic unit probe is the T5 section of
`engine/net/tests/storage_model_test.cpp` (shared file, owned by T4 per
GR-08 T1.8).

## Procedure (`device-lab/storage-permissions`)

1. Target an API-29+ device matrix (at least API 29 and the latest stable).
2. Case A (no broad access): exercise read/write/share on every route and
   confirm the app never requests `MANAGE_EXTERNAL_STORAGE` — install and
   run with only minimal media/picker permissions.
3. Case B (grant persistence): grant a SAF/Photo Picker URI with
   persistable access, force-stop and restart the app, then access the same
   URI — expect success without re-prompting.
4. Case C (non-persisted grant): access a URI granted without persistable
   rights after restart — expect denial (fail-closed), requiring a fresh
   user grant.

## Pass gate

Permission test green on the API-29+ matrix: read/write/share succeed with
`RequiresManageExternalStorage == false` on every route; a persisted grant
is accessible after restart while a non-persisted grant is not.
