// @muse/jsx — shared preset-name-only mixin (FR-MUSE-002, AC-MUSE-035).
// Components accept a preset name only; raw model/endpoint/parameter props
// are not part of this surface (tsc TS2322-class rejection) and dispatch
// throws unknown-preset / non-muse-model pre-upstream (AC-MUSE-036).
// PresetKey is `string` until preset-union codegen lands (Q-MUSE-003 slice).
export type PresetKey = string;

export interface MusePresetProps {
  preset?: PresetKey;
}
