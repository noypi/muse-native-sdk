// @muse/core public types (T11 wiring — FR-MCORE-001..007).
// One-way dependency: nothing in core requires native/jsx/cli.
// Banned-hook and unknown-lifecycle names are intentionally absent so that
// importing them fails typecheck (AC-MCORE-002 / AC-MCORE-012, packaging half).

export interface StateHandle<T = unknown> {
  value: T;
}

export interface ComputedHandle<T = unknown> {
  readonly value: T;
}

export interface StateOptions {
  deep?: boolean;
}

export interface WatchOptions {
  immediate?: boolean;
  flush?: 'post';
}

export type WatchSource<T = unknown> = StateHandle<T> | ComputedHandle<T> | (() => T);

export type CleanupFn = () => void;

export type Disposer = () => void;

export function state<T>(value: T, opts?: StateOptions): StateHandle<T>;
export function reactive<T extends object>(obj: T): T;
export function readonly<T>(value: T): T;
export function markRaw<T>(value: T): T;

export function computed<T>(getter: () => T): ComputedHandle<T>;

export function effect(fn: () => void | CleanupFn): Disposer;
export function watch<T>(
  source: WatchSource<T>,
  cb: (newValue: T, oldValue: T) => void,
  opts?: WatchOptions
): Disposer;
export function watchEffect(fn: () => void): Disposer;

export function batch(fn: () => void): void;

export type InstanceHandle = object;

export function createInstance(body: () => unknown): InstanceHandle;
export function mount(handle: InstanceHandle): void;
export function unmount(handle: InstanceHandle): void;
export function getCurrentInstance(): InstanceHandle | null;

export function onBeforeMount(fn: () => void): void;
export function onMounted(fn: () => void): void;
export function onBeforeUnmount(fn: () => void): void;
export function onUnmounted(fn: () => void): void;

export function onErrorCaptured(fn: (err: unknown) => void): void;
export function disposeInstance(handle: InstanceHandle): void;

export interface TwoWayBindingIr {
  mode: 'two-way';
  target: string;
  reads: string[];
  writes: string[];
}

export interface OneWayBindingIr {
  mode: 'one-way';
  expr: string;
}

export interface ValueBinding {
  pushInputValue<V>(v: V): void;
  pullStateValue<T>(): T;
  dispose(): void;
}

export interface ReadonlyBinding {
  dispose(): void;
}

export function bindValue<T>(
  stateHandle: StateHandle<T>,
  ir: TwoWayBindingIr
): ValueBinding;
export function bindReadonly<T>(
  source: StateHandle<T> | ComputedHandle<T>,
  ir: OneWayBindingIr,
  onChange: (v: unknown) => void
): ReadonlyBinding;

export interface SpringHandle {
  value: number;
  animateTo(target: number, opts?: { velocity?: number }): void;
  getVelocity(): number;
}

export function spring(initial: number): SpringHandle;
