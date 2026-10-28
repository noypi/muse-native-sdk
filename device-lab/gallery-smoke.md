# Gallery smoke — device-lab procedure (TC-SAMPLE-003 / AC-SAMPLE-003)

Scope: `examples/m3-gallery`. Verifies the gallery launches on a supported
device and on an emulator, renders every story in light and dark themes,
with no unhandled error. Execution is external/manual — not unit-verifiable.

App under test: `examples/m3-gallery/src/App.tsx` renders every story from
`buildStories(manifest)` (one story per component per documented
variant/state per theme) as a flat reachable list with a
`component variant state theme` label per entry.

## Prerequisites

- Gallery builds: `muse-native build examples/m3-gallery` exits 0.
- One supported physical device and one emulator image, both meeting the
  documented prerequisites.
- Gallery story coverage green: `npm run test:stories -w examples/m3-gallery`.

## Procedure (run twice: once per target)

1. Launch: `muse-native run examples/m3-gallery` on the physical device.
   Confirm the app launches with no unhandled error (no redbox / crash /
   unhandled-exception overlay).
2. Scroll through the full story list. Confirm every story entry renders its
   label (`component variant state theme`) and its rendered output.
3. Confirm both `light` and `dark` themed stories are present — for each
   component/variant/state there is a light entry and a dark entry.
4. Open every story (tap/scroll to each entry; entries are all on the one
   scrollable list — none hidden behind navigation). Confirm each renders
   with no unhandled error.
5. Repeat steps 1–4 on the emulator.

## Pass criteria (all must hold on BOTH targets)

- [ ] Launch succeeds with no unhandled error.
- [ ] Every story from `buildStories(manifest)` is reachable in the list.
- [ ] Every story renders in `light` and in `dark`.
- [ ] No unhandled error while opening every story.

## Record

Tester name, date, device model + OS, emulator image + OS, gallery commit,
and one line per target: PASS / FAIL with the failing story id
(`component/variant/state/theme`) or error text on failure.
