"use strict";

// T3 — component instance model (FR-MCORE-001).
//
// Body-once semantics: `createInstance(body)` runs `body` exactly once,
// during creation (mount setup), with `getCurrentInstance()` returning the
// new handle while the body executes. `mount`/`unmount` never re-enter the
// body; they only drive the attach/detach phases around hook firing and
// disposal. Both are idempotent so double-mount / double-unmount is safe.
//
// Ownership for later items (documented minimal hooks, driven by T9/T8):
// - `registerDisposer(handle, fn)` — every reactive value/effect/watch/hook
//   created during the body run registers its disposer here via the current
//   instance; `unmount` runs them in reverse order (LIFO), each at most once,
//   continuing past individual failures and rethrowing the first error.
// - `registerHook(handle, phase, fn)` — lifecycle phases owned by T8
//   (`onBeforeMount`/`onMounted`/`onBeforeUnmount`/`onUnmounted`); `mount`
//   fires `beforeMount` then `mounted`, `unmount` fires `beforeUnmount`,
//   disposes, then `unmounted`.
// - `registerErrorCapturer(handle, fn)` + `captureError(handle, err)` —
//   capturer registry + consult-on-throw-paths seam owned by T12 for T9's
//   `lib/scope.js` delivery (`onErrorCaptured` registers via the former;
//   every throw path below consults the latter nearest-ancestor-first:
//   swallow exactly-once when a capturer handles, identical-object rethrow
//   to the boundary caller when uncaptured).
// - `defineBannedImports(target)` — require-time half of AC-MCORE-002 for
//   T11 to wire into the package root: defines throwing getters for the
//   banned React-hook names so that accessing them throws
//   `MUSE_CORE_UNKNOWN_IMPORT` naming the import and `@muse/core`.
//   (Typecheck half is `index.d.ts` omitting the names — also T11's file.)
//
// CommonJS, dependency-free pure JS. All scheduling goes through T2's
// `packages/core/lib/scheduler.js`; this module schedules nothing.

var BANNED_HOOKS = ['useState', 'useEffect', 'useMemo', 'useCallback', 'useRef'];

var HOOK_PHASES = ['beforeMount', 'mounted', 'beforeUnmount', 'unmounted'];

// Stack of bodies currently executing (innermost last), so nested
// `createInstance` calls inside a body each observe their own handle and the
// outer handle is restored afterwards. Null outside any body run.
var instanceStack = [];

// Brand every handle created here; mount/unmount reject foreign objects.
var knownInstances = new WeakSet();

function bannedImportError(name) {
  var err = new Error(
    "MUSE_CORE_UNKNOWN_IMPORT: '" +
      name +
      "' is not exported by '@muse/core' — React hooks are not part of the @muse/core surface"
  );
  err.name = 'MUSE_CORE_UNKNOWN_IMPORT';
  err.code = 'MUSE_CORE_UNKNOWN_IMPORT';
  return err;
}

// Define throwing getters for each banned hook name on `target` (the T11
// wiring point: `defineBannedImports(module.exports)` from the package
// root). Accessing — or destructuring — any banned name throws the named
// error; all other properties on `target` are untouched.
function defineBannedImports(target) {
  if (target === null || (typeof target !== 'object' && typeof target !== 'function')) {
    throw new TypeError('defineBannedImports(target): target must be an object');
  }
  for (var i = 0; i < BANNED_HOOKS.length; i += 1) {
    (function (name) {
      try {
        Object.defineProperty(target, name, {
          configurable: true,
          enumerable: true,
          get: function () {
            throw bannedImportError(name);
          },
          set: function () {
            throw bannedImportError(name);
          },
        });
      } catch (defineErr) {
        // Non-configurable pre-existing property (e.g. a test double that
        // eagerly assigned the name): fall through — reads will surface
        // whatever value is there, and the package-root guard in T11 owns it.
        void defineErr;
      }
    })(BANNED_HOOKS[i]);
  }
  return target;
}

function assertHandle(handle, fnName) {
  if (!handle || typeof handle !== 'object' || !knownInstances.has(handle)) {
    throw new TypeError(fnName + '(handle): handle must be created by createInstance');
  }
}

function createInstance(body) {
  if (typeof body !== 'function') {
    throw new TypeError('createInstance(body): body must be a function');
  }
  var handle = {
    mounted: false,
    // Ancestor link: the body running when this handle was created (null at
    // root). Capture delivery walks it nearest-ancestor-first.
    parent: instanceStack.length > 0 ? instanceStack[instanceStack.length - 1] : null,
    // Error capturers registered via registerErrorCapturer (T12 seam for
    // T9's `onErrorCaptured`); consulted nearest-ancestor-first on throw.
    capturers: [],
    // Lifecycle hook lists, keyed by phase; T8 registers via registerHook.
    hooks: { beforeMount: [], mounted: [], beforeUnmount: [], unmounted: [] },
    // Owned disposers (effects/watches/cleanups/scopes); T6/T7/T9/T10
    // register via registerDisposer. Entries are `{ fn, disposed }`.
    disposers: [],
    // Opaque attach slot for a future renderer; mount/unmount toggle it.
    attached: false,
  };
  knownInstances.add(handle);
  instanceStack.push(handle);
  try {
    body();
  } catch (err) {
    // T12 consult-on-throw: offer the error to the nearest ancestor capturer
    // first (starting at this handle so a self-registered capturer also
    // applies). Swallow exactly-once when handled; otherwise rethrow the
    // identical object to the boundary caller (the root handler). The stack
    // is already popped below before delivery so capturers observe the
    // restored current instance.
    instanceStack.pop();
    if (captureError(handle, err)) return handle;
    throw err;
  }
  instanceStack.pop();
  return handle;
}

function getCurrentInstance() {
  if (instanceStack.length === 0) return null;
  return instanceStack[instanceStack.length - 1];
}

// Minimal disposal-registry hook for the later scope module (T9): records
// `fn` as owned by `handle`. `unmount` runs owned disposers; duplicates of
// the same fn reference are stored once.
function registerDisposer(handle, fn) {
  assertHandle(handle, 'registerDisposer');
  if (typeof fn !== 'function') {
    throw new TypeError('registerDisposer(handle, fn): fn must be a function');
  }
  var disposers = handle.disposers;
  for (var i = 0; i < disposers.length; i += 1) {
    if (disposers[i].fn === fn) return fn;
  }
  disposers.push({ fn: fn, disposed: false });
  return fn;
}

// Minimal lifecycle-registry hook for T8: records `fn` under `phase` on
// `handle`. Unknown phases throw; the named unknown-lifecycle import error
// itself is T8's surface.
function registerHook(handle, phase, fn) {
  assertHandle(handle, 'registerHook');
  if (HOOK_PHASES.indexOf(phase) === -1) {
    throw new TypeError(
      "registerHook(handle, phase, fn): unknown phase '" + phase + "'"
    );
  }
  if (typeof fn !== 'function') {
    throw new TypeError('registerHook(handle, phase, fn): fn must be a function');
  }
  handle.hooks[phase].push(fn);
  return fn;
}

// T12 capturer-registry seam for T9's `lib/scope.js` (`onErrorCaptured`
// registers here): records `fn` on `handle`'s capturer stack and returns an
// idempotent unregister disposer. Delivery (`captureError`, below) consults
// nearest-ancestor-first and stops at the first instance that owns capturers.
function registerErrorCapturer(handle, fn) {
  assertHandle(handle, 'registerErrorCapturer');
  if (typeof fn !== 'function') {
    throw new TypeError('registerErrorCapturer(handle, fn): fn must be a function');
  }
  if (!Array.isArray(handle.capturers)) handle.capturers = [];
  handle.capturers.push(fn);
  var removed = false;
  return function unregisterErrorCapturer() {
    if (removed) return;
    removed = true;
    var list = handle.capturers;
    if (!Array.isArray(list)) return;
    for (var i = 0; i < list.length; i += 1) {
      if (list[i] === fn) {
        list.splice(i, 1);
        return;
      }
    }
  };
}

// T12 consult step callable from `lib/scope.js`: walk `handle` then its
// `parent` chain nearest-first; invoke every capturer owned by the first
// instance that has any (each exactly once, in registration order) and
// return true (handled — caller swallows). Return false when no capturer
// exists anywhere on the chain (caller rethrows the identical object).
// A throwing capturer propagates its own error and counts as unhandled here
// only in that the original error is replaced by the capturer's throw.
function captureError(handle, err) {
  var current = handle || null;
  while (current !== null && typeof current === 'object') {
    var list = current.capturers;
    if (Array.isArray(list) && list.length > 0) {
      var fns = list.slice();
      for (var i = 0; i < fns.length; i += 1) {
        fns[i](err);
      }
      return true;
    }
    if (!Object.prototype.hasOwnProperty.call(current, 'parent')) break;
    current = current.parent;
  }
  return false;
}

function runHooks(handle, phase) {
  var fns = handle.hooks[phase].slice();
  for (var i = 0; i < fns.length; i += 1) {
    try {
      fns[i]();
    } catch (err) {
      // T12 consult-on-throw for hook firing (mount/unmount paths): capture
      // at the throwing handle's chain, swallow when handled, otherwise
      // rethrow the identical object to the boundary caller.
      if (captureError(handle, err)) return;
      throw err;
    }
  }
}

// Run owned disposers LIFO, each at most once; keep going past individual
// failures and rethrow the first error after the registry is drained.
function disposeOwned(handle) {
  var firstError = null;
  var disposers = handle.disposers;
  for (var i = disposers.length - 1; i >= 0; i -= 1) {
    var entry = disposers[i];
    if (entry.disposed) continue;
    entry.disposed = true;
    try {
      entry.fn();
    } catch (err) {
      if (firstError === null) firstError = err;
    }
  }
  disposers.length = 0;
  if (firstError !== null && firstError !== undefined) throw firstError;
}

function mount(handle) {
  assertHandle(handle, 'mount');
  if (handle.mounted) return handle;
  runHooks(handle, 'beforeMount');
  handle.attached = true; // subtree attach point (no renderer in T3).
  handle.mounted = true;
  runHooks(handle, 'mounted');
  return handle;
}

function unmount(handle) {
  assertHandle(handle, 'unmount');
  if (!handle.mounted && !handle.attached && handle.disposers.length === 0) {
    // Never-mounted (or fully settled) handle: idempotent safe no-op that
    // still drains any late-registered disposers for uniformity.
    disposeOwned(handle);
    return handle;
  }
  runHooks(handle, 'beforeUnmount');
  disposeOwned(handle);
  handle.attached = false; // subtree detach point (no renderer in T3).
  handle.mounted = false;
  runHooks(handle, 'unmounted');
  return handle;
}

module.exports = {
  createInstance: createInstance,
  mount: mount,
  unmount: unmount,
  getCurrentInstance: getCurrentInstance,
  // Registry hooks owned by later items (T8 lifecycle, T9 scope, T12 seam).
  registerDisposer: registerDisposer,
  registerHook: registerHook,
  registerErrorCapturer: registerErrorCapturer,
  captureError: captureError,
  // Banned-import require-time helper + metadata for T11's root wiring.
  defineBannedImports: defineBannedImports,
  bannedImportError: bannedImportError,
  BANNED_HOOKS: BANNED_HOOKS.slice(),
};
