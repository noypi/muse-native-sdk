// AC-M3-023 — selection-handle surface types (T1 Checkbox; T2 Radio).
// Constructor-injected deps contracts mirroring `textinput.js`: state plus
// the two-way binding contract (FR-TSX-006 / FR-MCORE-007) arrive as factory
// parameters, never imported.

export type SelectionBinding = {
  value?: boolean;
  set?(value: boolean): void;
};

export type CheckboxDeps = {
  checked: boolean;
  enabled?: boolean;
  binding?: SelectionBinding;
  onCheckedChange?: (value: boolean) => void;
};

export type CheckboxTapRecord = {
  checked: boolean;
  repainted: number;
  reinstantiated: 0;
  frames: 1;
};

export type CheckboxHandle = {
  tap(): CheckboxTapRecord;
  readonly checked: boolean;
};

export type RadioDeps = {
  selected: boolean;
  enabled?: boolean;
  binding?: SelectionBinding;
  onSelectedChange?: (value: boolean) => void;
};

export type RadioTapRecord = {
  selected: boolean;
  repainted: number;
  reinstantiated: 0;
  frames: 1;
};

export type RadioHandle = {
  tap(): RadioTapRecord;
  readonly selected: boolean;
};

export declare function createCheckbox(deps: CheckboxDeps): CheckboxHandle;
export declare function createRadio(deps: RadioDeps): RadioHandle;
