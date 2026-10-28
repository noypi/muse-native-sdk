// FR-M3-001 / AC-M3-001 — @muse/native tier-1 prop types (T3).
// Per-primitive prop types generated from the T1 manifest schemas in
// muse.manifest.json: `token` roles map to string, `slot` maps to unknown,
// `values` lists map to string-literal unions, non-required props are
// optional. Runtime passes props through; key equality lives here.

export type NativeProps = Record<string, unknown>;
export type NativeDescriptor = { type: string; props: NativeProps };

export type SurfaceProps = {
  tone?: string;
  elevation?: number;
};

export type TextProps = {
  variant?: string;
  color?: string;
};

export type IconProps = {
  name: string;
  size?: number;
  color?: string;
};

export type ImageProps = {
  source: string;
  contentFit?: 'cover' | 'contain' | 'fill' | 'none';
};

export type RippleProps = {
  color?: string;
  bounded?: boolean;
};

export type DividerProps = {
  orientation?: 'horizontal' | 'vertical';
  color?: string;
};

export type ScrollProps = {
  orientation?: 'vertical' | 'horizontal' | 'both';
  content?: unknown;
};

export type StackProps = {
  direction?: 'row' | 'column';
  spacing?: number;
  children?: unknown;
};

export type TextFieldEditCoreProps = {
  value?: string;
  placeholder?: string;
  enabled?: boolean;
};

export type OverlayHostProps = {
  content?: unknown;
};

// FR-M3-002 / AC-M3-004 — @muse/native tier-2 prop types (T5, T6).
// Per-component prop types generated from the T2 manifest schemas in
// muse.manifest.json: `token` roles map to string, `slot` maps to unknown,
// `values` lists map to string-literal unions, non-required props are
// optional. Variants are prop values, not component names. The named
// *Variant aliases below are generated from the manifest `values` lists so
// types and manifest cannot drift (RULE-M3-003); `tsc --noEmit` rejects any
// value outside these unions.

export type ButtonVariant = 'filled' | 'tonal' | 'outlined' | 'text' | 'elevated';
export type CardVariant = 'elevated' | 'filled' | 'outlined';
export type ChipVariant = 'assist' | 'filter' | 'input' | 'suggestion';

export type ButtonProps = {
  variant?: ButtonVariant;
  label: string;
  enabled?: boolean;
  icon?: string;
};

export type IconButtonProps = {
  variant?: 'standard' | 'filled' | 'tonal' | 'outlined';
  icon: string;
  enabled?: boolean;
  selected?: boolean;
};

export type FabProps = {
  size?: 'small' | 'medium' | 'large';
  icon: string;
  enabled?: boolean;
};

export type ExtendedFabProps = {
  label: string;
  icon?: string;
  enabled?: boolean;
};

export type CardProps = {
  variant?: CardVariant;
  content?: unknown;
  enabled?: boolean;
};

export type ChipProps = {
  variant?: ChipVariant;
  label: string;
  selected?: boolean;
  enabled?: boolean;
};

export type CheckboxProps = {
  checked?: boolean;
  enabled?: boolean;
  error?: boolean;
};

export type RadioProps = {
  selected?: boolean;
  enabled?: boolean;
  error?: boolean;
};

export type SwitchProps = {
  checked?: boolean;
  enabled?: boolean;
  icon?: string;
};

export type SliderProps = {
  value: number;
  enabled?: boolean;
};

export type BadgeProps = {
  label?: string;
  content?: unknown;
};

export type LinearProgressProps = {
  progress?: number;
  color?: string;
};

export type CircularProgressProps = {
  progress?: number;
  color?: string;
};

export type ListItemProps = {
  headline: string;
  supporting?: string;
  icon?: string;
  enabled?: boolean;
};

export type AvatarProps = {
  size?: 'small' | 'medium' | 'large';
  source?: string;
  label?: string;
};

export type TooltipProps = {
  text: string;
  anchor?: unknown;
};

// FR-M3-006 — `TextInput` tier-2 component over the tier-1 text-field edit
// core (T1). Prop keys mirror the `TextInput` manifest schema exactly.
export type TextSelection = { start: number; end: number };
export type TextInputProps = {
  value?: string;
  selection?: TextSelection;
  composing?: string;
  contentType?: 'plain' | 'rich';
  onSelectionChange?: (TextSelection) => void;
};

export type EditingEngine = {
  setComposingText(s: string): void;
  commitText(s: string): void;
  cancelComposition(): void;
  getText(): string;
  setSelection(start: number, end: number): void;
  getSelection(): { start: number; end: number };
};

export type PlatformClipboard = {
  readText(): string | Promise<string>;
  writeText(s: string): void | Promise<void>;
  hasText(): boolean;
};

export type InsetFrames = {
  current(): { imeBottom: number };
  subscribe(fn: (frame: { imeBottom: number }) => void): () => void;
};

export type TwoWayBinding = {
  value: string;
  set?(value: string): void;
};

export type TextInputHandle = {
  setComposingText(s: string): void;
  commitText(s: string): void;
  cancelComposition(): void;
  setSelection(start: number, end: number): void;
  getSelection(): { start: number; end: number };
  cut(): void;
  copy(): void;
  paste(): void | Promise<void>;
  selectAll(): void;
  value: string;
  selection: { start: number; end: number };
  dispose(): void;
};

export declare function createTextInput(deps: {
  editingEngine: EditingEngine;
  clipboard: PlatformClipboard;
  insets: InsetFrames;
  binding?: TwoWayBinding;
}): TextInputHandle;

export type Tier2Props =
  | ButtonProps
  | IconButtonProps
  | FabProps
  | ExtendedFabProps
  | CardProps
  | ChipProps
  | CheckboxProps
  | RadioProps
  | SwitchProps
  | SliderProps
  | BadgeProps
  | LinearProgressProps
  | CircularProgressProps
  | ListItemProps
  | AvatarProps
  | TooltipProps;

export declare function Button(props?: ButtonProps): NativeDescriptor;
export declare function IconButton(props?: IconButtonProps): NativeDescriptor;
export declare function Fab(props?: FabProps): NativeDescriptor;
export declare function ExtendedFab(props?: ExtendedFabProps): NativeDescriptor;
export declare function Card(props?: CardProps): NativeDescriptor;
export declare function Chip(props?: ChipProps): NativeDescriptor;
export declare function Checkbox(props?: CheckboxProps): NativeDescriptor;
export declare function Radio(props?: RadioProps): NativeDescriptor;
export declare function Switch(props?: SwitchProps): NativeDescriptor;
export declare function Slider(props?: SliderProps): NativeDescriptor;
export declare function Badge(props?: BadgeProps): NativeDescriptor;
export declare function LinearProgress(props?: LinearProgressProps): NativeDescriptor;
export declare function CircularProgress(props?: CircularProgressProps): NativeDescriptor;
export declare function ListItem(props?: ListItemProps): NativeDescriptor;
export declare function Avatar(props?: AvatarProps): NativeDescriptor;
export declare function Tooltip(props?: TooltipProps): NativeDescriptor;
export declare function TextInput(props?: TextInputProps): NativeDescriptor;

export type Tier2Name =
  | 'Button'
  | 'IconButton'
  | 'Fab'
  | 'ExtendedFab'
  | 'Card'
  | 'Chip'
  | 'Checkbox'
  | 'Radio'
  | 'Switch'
  | 'Slider'
  | 'Badge'
  | 'LinearProgress'
  | 'CircularProgress'
  | 'ListItem'
  | 'Avatar'
  | 'Tooltip'
  | 'TextInput';

export declare function Surface(props?: SurfaceProps): NativeDescriptor;
export declare function Text(props?: TextProps): NativeDescriptor;
export declare function Icon(props?: IconProps): NativeDescriptor;
export declare function Image(props?: ImageProps): NativeDescriptor;
export declare function Ripple(props?: RippleProps): NativeDescriptor;
export declare function Divider(props?: DividerProps): NativeDescriptor;
export declare function Scroll(props?: ScrollProps): NativeDescriptor;
export declare function Stack(props?: StackProps): NativeDescriptor;
export declare function TextFieldEditCore(props?: TextFieldEditCoreProps): NativeDescriptor;
export declare function OverlayHost(props?: OverlayHostProps): NativeDescriptor;

export type Tier1Name =
  | 'Surface'
  | 'Text'
  | 'Icon'
  | 'Image'
  | 'Ripple'
  | 'Divider'
  | 'Scroll'
  | 'Stack'
  | 'TextFieldEditCore'
  | 'OverlayHost';

export declare const primitives: string[];
export declare const components: Record<
  Tier1Name | Tier2Name,
  (props?: Record<string, unknown>) => NativeDescriptor
>;
export declare const manifest: {
  name: '@muse/native';
  components: Array<{ name: string; props: unknown }>;
};
