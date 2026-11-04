'use strict';

// muse-demo tab navigation facade (T2, FR-NAV-011/012/004).
//
// Thin facade over the engine seam `RouterBridge.invoke("navigate",
// {route, params})` — no engine changes. Mirrors `galleryNav.js` shape:
// link shapes `muse-demo://tab/<lens|field|list>` resolve through the
// single bridge path; unknown routes fail closed with
// `{ code: 'unknown-route', link }`, keeping current page/history
// (never blank). `push` navigates with back-stack push, `replace`
// retargets without a history entry.
//
// Emission seam: when the hermetic test sink
// `globalThis.__museTestBroadcasts` (an array) is present, one record
// `{ action: 'muse.native.NAVIGATE', route, params, origin }` is appended
// per allowed call. Otherwise the injected host entry
// `globalThis.__museRouterNavigate(route, params, origin)` is called when
// present; a host entry that throws or returns exactly `false` denies.
// With neither sink nor host, navigation still applies in-memory (the
// demo shell owns its tab state) without emission.

const NAVIGATE_ACTION = 'muse.native.NAVIGATE';
const TAB_LINK_PREFIX = 'muse-demo://tab/';
const DEMO_TABS = ['lens', 'field', 'list'];

// T3b (fallback-kinds, AC-SAMPLE-023): demo-kind emission seam. Every demo
// descriptor comp is a gate-known generic literal applied by hand at author
// time (lens→Image, field→TextInput, list→Card, ErrorSnackbar→SnackbarHost).
// No runtime mapper runs on glass: the bundler serializes without executing,
// so the baked tree must CONTAIN the literals. Unknown kinds fail closed
// with a named `unknown demo kind '<kind>'` throw at build/dev time (never
// silent substitution, never an unmapped demo kind at runtime).

// demoCompFor(demoKind) — the single demo→gate comp seam for this module.
// Author-time literal table (mirrors the descriptor comps below); unknown
// kinds fail closed NAMED here.
const DEMO_COMP_FOR_TAB = {
  lens: 'Image',
  field: 'TextInput',
  list: 'Card',
  home: 'Image',
  ErrorSnackbar: 'SnackbarHost',
};
function demoCompFor(demoKind) {
  if (typeof demoKind === 'string' && Object.hasOwn(DEMO_COMP_FOR_TAB, demoKind)) {
    return DEMO_COMP_FOR_TAB[demoKind];
  }
  throw new Error(`unknown demo kind '${String(demoKind)}'`);
}

function isArbitraryRouteEnabled(env) {
  const source = env !== null && typeof env === 'object' ? env : {};
  const raw =
    typeof globalThis !== 'undefined' &&
    globalThis.process !== undefined &&
    globalThis.process.env !== undefined &&
    (env === undefined || env === null)
      ? globalThis.process.env.MUSE_ARBITRARY_ROUTE_ENABLED
      : source.MUSE_ARBITRARY_ROUTE_ENABLED;
  if (raw === '0' || raw === 0 || raw === false) {
    return false;
  }
  return true;
}

function parseTabLink(link) {
  if (typeof link !== 'string' || !link.startsWith(TAB_LINK_PREFIX)) {
    return { code: 'unknown-route', link: typeof link === 'string' ? link : link };
  }
  const rest = link.slice(TAB_LINK_PREFIX.length);
  if (!DEMO_TABS.includes(rest)) {
    return { code: 'unknown-route', link };
  }
  return { id: rest };
}

function emitNavigate(route, params, origin) {
  const navigateParams =
    params !== null && typeof params === 'object' && !Array.isArray(params) ? params : {};
  if (typeof globalThis !== 'undefined' && Array.isArray(globalThis.__museTestBroadcasts)) {
    globalThis.__museTestBroadcasts.push({
      action: NAVIGATE_ACTION,
      route,
      params: navigateParams,
      origin,
    });
    return true;
  }
  try {
    const host =
      typeof globalThis !== 'undefined' ? globalThis.__museRouterNavigate : undefined;
    if (typeof host === 'function') {
      const issued = host(route, navigateParams, origin);
      if (issued === false) {
        return false;
      }
      return true;
    }
  } catch (err) {
    void err;
    return false;
  }
  // No sink and no host entry: in-memory navigation still applies; no
  // bridge record exists to emit. Report issued (no denial).
  return true;
}

function createDemoNavigator() {
  let history = [];
  let current = null;

  function push(route, params) {
    const parsed = parseTabLink(route);
    if (parsed.code) {
      return { code: 'unknown-route', link: typeof route === 'string' ? route : route };
    }
    const ok = emitNavigate(route, params, 'shell-chrome');
    if (!ok) {
      return { code: 'unknown-route', link: typeof route === 'string' ? route : route };
    }
    history.push(parsed.id);
    current = parsed.id;
    return { page: parsed.id };
  }

  function replace(route, params) {
    const parsed = parseTabLink(route);
    if (parsed.code) {
      return { code: 'unknown-route', link: typeof route === 'string' ? route : route };
    }
    const ok = emitNavigate(route, params, 'shell-chrome');
    if (!ok) {
      return { code: 'unknown-route', link: typeof route === 'string' ? route : route };
    }
    if (history.length === 0) {
      history.push(parsed.id);
    } else {
      history[history.length - 1] = parsed.id;
    }
    current = parsed.id;
    return { page: parsed.id };
  }

  function historyOf() {
    return history.slice();
  }

  function currentOf() {
    return current;
  }

  return {
    push,
    replace,
    history: historyOf,
    current: currentOf,
  };
}

const shared = createDemoNavigator();

// --- Facade binding (T5/T6, FR-NAV-014 thin-host) ---------------------------
// Link-shape validation lives here via `createRoutes` descriptors +
// `@muse/router` beforeEach guard + typed-params validation. The host
// forwards inbound VIEW intents as opaque strings; `forwardIntent` resolves
// them to the same dest as `push`. Outcomes/logs are observed `NavResult`s.
const { createRoutes, createRouter } = require('../../../packages/router/src/index.js');

const DEMO_HOME = 'lens';

const demoDescriptors = [
  { id: 'home', link: 'muse-demo://tab/home', params: {}, comp: 'Image' },
  { id: 'lens', link: 'muse-demo://tab/lens', params: {}, comp: 'Image' },
  { id: 'field', link: 'muse-demo://tab/field', params: {}, comp: 'TextInput' },
  { id: 'list', link: 'muse-demo://tab/list', params: {}, comp: 'Card' },
];

// Build/dev-time gate: every demo tab kind in the descriptor table must
// resolve through `demoCompFor` (fail closed NAMED on unmapped). The
// `home` alias is not a demo kind — it resolves to DEMO_HOME, whose comp
// is validated via the same seam.
for (const tab of DEMO_TABS) {
  demoCompFor(tab);
}
demoCompFor(DEMO_HOME);

const DEMO_LINK_RE = /^muse-demo:\/\/tab\/([A-Za-z0-9-]+)(\?.*)?$/;

function linkToId(opaque) {
  if (typeof opaque !== 'string') {
    return undefined;
  }
  const m = DEMO_LINK_RE.exec(opaque);
  if (!m || m[2] !== undefined) {
    return undefined;
  }
  const id = m[1];
  if (id !== 'lens' && id !== 'field' && id !== 'list' && id !== 'home') {
    return undefined;
  }
  return id;
}

function normalizeTo(to) {
  if (typeof to === 'string' && to.indexOf('://') !== -1) {
    return linkToId(to);
  }
  if (to && typeof to === 'object' && typeof to.id === 'string') {
    return to.id;
  }
  return to;
}

function createDemoRouter({ transport } = {}) {
  const routes = createRoutes(demoDescriptors);
  const router = createRouter({ routes, home: DEMO_HOME, transport });
  router.beforeEach(async () => true);
  const rawPush = router.push.bind(router);
  const rawReplace = router.replace.bind(router);

  // T9 (AC-NAV-041): unbound bridge fails closed. When no transport was
  // injected (nothing bound `globalThis.RouterBridge` at boot), navigation
  // cannot forward — return a named denial, never throw, never silently
  // succeed, page unchanged. Never a silent no-op (T4 mode).
  const bound =
    transport !== null &&
    typeof transport === 'object' &&
    typeof transport.invoke === 'function';
  function unboundDenial() {
    return { ok: false, code: 'transport-error' };
  }

  async function forwardIntent(opaque) {
    if (!bound) {
      return unboundDenial();
    }
    const id = linkToId(opaque);
    if (id === undefined) {
      return { ok: false, code: 'unknown-route' };
    }
    return rawPush(id);
  }

  const TAB_ORDER = ['lens', 'field', 'list'];

  async function tapAdvance() {
    if (!bound) {
      return unboundDenial();
    }
    const cur = router.current;
    const idx = TAB_ORDER.indexOf(cur);
    // Single computation: idx -1 covers home/unknown current (fail-closed to 'field').
    const next = idx === -1 ? 'field' : TAB_ORDER[(idx + 1) % TAB_ORDER.length];
    return rawPush(next);
  }

  async function onFallbackRecover() {
    if (!bound) {
      return unboundDenial();
    }
    const cur = router.current || DEMO_HOME;
    const res = await rawReplace(cur);
    if (typeof console !== 'undefined' && console.debug) {
      console.debug('demo recover dest=' + cur + ' ok=' + (res && res.ok));
    }
    return res;
  }

  async function push(to, params) {
    if (!bound) {
      return unboundDenial();
    }
    const id = normalizeTo(to);
    if (typeof id !== 'string') {
      return { ok: false, code: 'unknown-route' };
    }
    if (params !== undefined && (typeof params !== 'object' || params === null || Array.isArray(params))) {
      return { ok: false, code: 'bad-params' };
    }
    return rawPush(params !== undefined ? { id, params } : id);
  }

  async function replace(to, params) {
    if (!bound) {
      return unboundDenial();
    }
    const id = normalizeTo(to);
    if (typeof id !== 'string') {
      return { ok: false, code: 'unknown-route' };
    }
    if (params !== undefined && (typeof params !== 'object' || params === null || Array.isArray(params))) {
      return { ok: false, code: 'bad-params' };
    }
    return rawReplace(params !== undefined ? { id, params } : id);
  }

  return {
    get current() {
      return router.current;
    },
    beforeEach: router.beforeEach.bind(router),
    push,
    replace,
    forwardIntent,
    tapAdvance,
    onFallbackRecover,
  };
}

// Cold-start drain helper (AC-NAV-044 stash rule, AC-NAV-041 transport).
// Pulls the taken opaque string via `consume` exactly once and delivers it
// through `forwardIntent` (same RouterBridge.invoke channel as warm intents).
// Never parses here; unknown/invalid input fails closed via forwardIntent.
// Returns null when nothing was stashed; a rejecting transport fails closed
// with a named denial; never throws.
async function drainStashedOpaqueLink(router, consume) {
  let opaque;
  try {
    opaque = typeof consume === 'function' ? consume() : undefined;
  } catch (err) {
    void err;
    return { ok: false, code: 'unknown-route' };
  }
  if (opaque === null || opaque === undefined || opaque === '') {
    return null;
  }
  try {
    return await router.forwardIntent(opaque);
  } catch (err) {
    void err;
    return { ok: false, code: 'transport-error' };
  }
}

// Opaque-link consumer seam (AC-NAV-044 stash rule, AC-NAV-041 transport).
// The platform/engine owns the backing take-and-clear consume (bound to the
// host stash); this adapter owns the `__museConsumeOpaqueLink` global so app
// code never touches it directly. First-wins: an engine-installed producer
// keeps priority; fail-closed on non-functions; never throws.
function installOpaqueLinkConsumer(consume) {
  try {
    if (typeof consume !== 'function') {
      return false;
    }
    if (typeof globalThis === 'undefined') {
      return false;
    }
    if (typeof globalThis.__museConsumeOpaqueLink === 'function') {
      return false;
    }
    globalThis.__museConsumeOpaqueLink = consume;
    return true;
  } catch (err) {
    void err;
    return false;
  }
}

// Adapter read of the installed consumer (undefined when none installed).
// Never throws.
function getOpaqueLinkConsumer() {
  try {
    if (typeof globalThis === 'undefined') {
      return undefined;
    }
    const consume = globalThis.__museConsumeOpaqueLink;
    return typeof consume === 'function' ? consume : undefined;
  } catch (err) {
    void err;
    return undefined;
  }
}

// T9 boot binding (AC-NAV-041): app-owned adapter binds the default
// `globalThis.RouterBridge` once at module load so `demoTransport()` (App
// boot) is non-null on device. First-wins: an engine/host-installed bridge
// keeps priority; fail-closed on missing globalThis; never throws.
//
// BUG-NAV-005: the default bridge resolves `invoke` in-memory (`{ok:true}`)
// and MUST NOT self-install outside the hermetic test sink — on device it
// froze the stub in place before `demoTransport()` captured it, so every tap
// resolved ok:true in JS while the C++ commit gate stayed unreachable
// (AC-NAV-041/042 violation, JS/C++ divergence). The in-memory stub now
// installs at module load ONLY when the hermetic test sink
// (`globalThis.__museTestBroadcasts`, the same test-only path the emission
// seam gates on) is present; elsewhere the bridge stays unbound until the
// platform binds the live (JNI-backed `RouterBridge.invoke`) entry, and the
// unbound router fails closed with a named `transport-error` (never a
// silent success, page unchanged). The explicit `ensureRouterBridge()`
// export below keeps its first-wins install semantics for direct callers
// (hermetic doubles pin it); only the automatic self-install is gated.
// Route validity stays with the route table + guards (JS-side pre-invoke
// checks) AND the C++ commit gate behind the live entry.
function hermeticTestSinkPresent() {
  try {
    return (
      typeof globalThis !== 'undefined' &&
      Array.isArray(globalThis.__museTestBroadcasts)
    );
  } catch (err) {
    void err;
    return false;
  }
}
function ensureRouterBridge() {
  try {
    if (typeof globalThis === 'undefined') {
      return false;
    }
    const existing = globalThis.RouterBridge;
    if (
      existing !== null &&
      typeof existing === 'object' &&
      typeof existing.invoke === 'function'
    ) {
      return true;
    }
    globalThis.RouterBridge = {
      invoke: async () => ({ ok: true }),
    };
    return true;
  } catch (err) {
    void err;
    return false;
  }
}

// BUG-NAV-005: the automatic boot self-install fires ONLY under the
// hermetic test sink (see above) — production module load leaves the
// bridge unbound until the platform binds the live entry first-wins.
if (hermeticTestSinkPresent()) {
  ensureRouterBridge();
}

module.exports = {
  createDemoNavigator,
  demoCompFor,
  push: shared.push,
  replace: shared.replace,
  parseTabLink,
  isArbitraryRouteEnabled,
  NAVIGATE_ACTION,
  TAB_LINK_PREFIX,
  DEMO_TABS,
  demoDescriptors,
  createDemoRouter,
  drainStashedOpaqueLink,
  installOpaqueLinkConsumer,
  getOpaqueLinkConsumer,
  ensureRouterBridge,
};
