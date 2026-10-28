# Device-lab gate — storage model (TC-NET-006)

External gate for FR-NET-003 / AC-NET-006. This directory carries the
procedure note only (`none` beyond this for the device gate); the hermetic
unit probe is `engine/net/tests/storage_model_test.cpp`.

## Procedure (`device-lab/storage-model`)

1. Target an API-29+ device (scoped storage enforced).
2. Case A (app-specific): write then read a file in the app-specific
   directory — expect success with no permission prompt.
3. Case B (MediaStore): insert then read a media item via MediaStore —
   expect success with only the media permission.
4. Case C (SAF / Photo Picker): open a user document via SAF and an image
   via the Photo Picker — expect success through the system picker with a
   persisted grant, and no `MANAGE_EXTERNAL_STORAGE` request.
5. Case D (FileProvider): share a file outbound — expect a `content://`
   URI served through FileProvider, never a raw file path.
6. Repeat read/write/share on all five routes and confirm correct route
   selection.

## Pass gate

All five routes read/write/share green against the device backend with
correct route selection.
