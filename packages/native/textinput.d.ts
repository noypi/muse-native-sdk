// FR-M3-006 / RULE-M3-004 — `TextInput` component surface types (T1).
// Constructor-injected engine-adapter contracts (FR-TEXT-008) plus the
// two-way binding contract (FR-TSX-006 / FR-MCORE-007). Engines are injected
// as factory parameters, never imported.

import type { NativeDescriptor } from './index';

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

export type TextInputDeps = {
  editingEngine: EditingEngine;
  clipboard: PlatformClipboard;
  insets: InsetFrames;
  binding?: TwoWayBinding;
  onSelectionChange?: (sel: { start: number; end: number }) => void;
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
  insetOffset: number;
  followInsets(): () => void;
  dispose(): void;
};

export type TextInputProps = {
  value?: string;
  selection?: { start: number; end: number };
  composing?: string;
  contentType?: 'plain' | 'rich';
  onSelectionChange?: (sel: { start: number; end: number }) => void;
};

export declare function createTextInput(deps: TextInputDeps): TextInputHandle;
export declare function TextInput(props?: TextInputProps): NativeDescriptor;
