// FR-M3 lane — @muse/native type skeleton for the nine stub primitives.
// FR-M3 fills in real prop schemas later; this file only names the exports.

export type NativeProps = Record<string, unknown>;
export type NativeDescriptor = { type: string; props: NativeProps };

export declare const primitives: string[];
export declare const components: Record<string, (props?: NativeProps) => NativeDescriptor>;
export declare const manifest: {
  name: '@muse/native';
  components: Array<{ name: string; props: unknown }>;
};
