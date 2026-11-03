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
  { id: 'home', link: 'muse-demo://tab/home', params: {} },
  { id: 'lens', link: 'muse-demo://tab/lens', params: {} },
  { id: 'field', link: 'muse-demo://tab/field', params: {} },
  { id: 'list', link: 'muse-demo://tab/list', params: {} },
];

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

  async function forwardIntent(opaque) {
    const id = linkToId(opaque);
    if (id === undefined) {
      return { ok: false, code: 'unknown-route' };
    }
    return rawPush(id);
  }

  const TAB_ORDER = ['lens', 'field', 'list'];

  async function tapAdvance() {
    const cur = router.current;
    const idx = TAB_ORDER.indexOf(cur);
    const next = idx === -1 ? 'field' : TAB_ORDER[(idx + 1) % TAB_ORDER.length];
    if (cur === 'lens' && TAB_ORDER.indexOf(cur) !== -1) {
      // Cycle step from the home tab lands on 'field', matching push('field').
      return rawPush('field');
    }
    return rawPush(next);
  }

  async function onFallbackRecover() {
    const cur = router.current || DEMO_HOME;
    const res = await rawReplace(cur);
    if (typeof console !== 'undefined' && console.debug) {
      console.debug('demo recover dest=' + cur + ' ok=' + (res && res.ok));
    }
    return res;
  }

  async function push(to, params) {
    const id = normalizeTo(to);
    if (typeof id !== 'string') {
      return { ok: false, code: 'unknown-route' };
    }
    return rawPush(params !== undefined ? { id, params } : id);
  }

  async function replace(to, params) {
    const id = normalizeTo(to);
    if (typeof id !== 'string') {
      return { ok: false, code: 'unknown-route' };
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

module.exports = {
  createDemoNavigator,
  push: shared.push,
  replace: shared.replace,
  parseTabLink,
  isArbitraryRouteEnabled,
  NAVIGATE_ACTION,
  TAB_LINK_PREFIX,
  DEMO_TABS,
  demoDescriptors,
  createDemoRouter,
};
