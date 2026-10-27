// FR-MCORE lane — @muse/core reactivity surface (stub skeleton).
// MCORE fills in real behavior later without renaming these exports.
// One-way dependency: nothing in core requires native/jsx/cli.

export interface StateHandle<T = unknown> {
  value: T;
}

export interface ComputedHandle<T = unknown> {
  value: T | undefined;
}

export function state<T>(initialValue: T): StateHandle<T>;
export function computed<T>(fn: () => T): ComputedHandle<T>;
export function effect(fn: () => void): void;
export function batch(fn: () => void): void;
export function onMount(fn: () => void): void;
export function onUnmount(fn: () => void): void;
