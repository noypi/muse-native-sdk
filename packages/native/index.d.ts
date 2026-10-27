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
  Tier1Name,
  (props?: Record<string, unknown>) => NativeDescriptor
> & Record<string, (props?: NativeProps) => NativeDescriptor>;
export declare const manifest: {
  name: '@muse/native';
  components: Array<{ name: string; props: unknown }>;
};
