'use strict';

function createRoutes(defs) {
  const routes = {};
  for (const def of defs || []) {
    routes[def.id] = {
      id: def.id,
      link: def.link,
      params: def.params || {},
      ...(def.beforeEnter ? { beforeEnter: def.beforeEnter } : {}),
    };
  }
  return routes;
}

function normalizeDest(dest, current) {
  if (typeof dest === 'function') {
    dest = dest(current);
  }
  if (typeof dest === 'string') {
    return { id: dest, params: {} };
  }
  if (dest && typeof dest === 'object') {
    return { id: dest.id, params: dest.params || {} };
  }
  return { id: undefined, params: {} };
}

function paramsValid(schema, params) {
  const s = schema || {};
  const p = params || {};
  for (const key of Object.keys(s)) {
    if (Object.prototype.hasOwnProperty.call(p, key)) {
      if (typeof p[key] !== s[key]) {
        return false;
      }
    }
  }
  return true;
}

function createRouter({ routes, home, transport }) {
  const guards = [];
  const stack = [home];

  const router = {
    get current() {
      return stack[stack.length - 1];
    },
    beforeEach(fn) {
      guards.push(fn);
    },
    push(dest) {
      return navigate(dest, false);
    },
    replace(dest) {
      return navigate(dest, true);
    },
    back() {
      return go(-1);
    },
    go(delta) {
      return go(delta);
    },
  };

  async function go(delta) {
    const n = Number(delta);
    if (n > 0) {
      return { ok: false, code: 'no-forward' };
    }
    if (n === 0 || Number.isNaN(n)) {
      return { ok: true };
    }
    let idx = stack.length - 1 + n;
    if (idx < 0) {
      idx = 0;
    }
    const target = stack[idx];
    stack.length = idx + 1;
    return { ok: true };
  }

  async function navigate(dest, isReplace) {
    const from = stack[stack.length - 1];
    const { id: to, params } = normalizeDest(dest, from);
    const route = routes ? routes[to] : undefined;
    if (!route) {
      return { ok: false, code: 'unknown-route' };
    }
    if (!paramsValid(route.params, params)) {
      return { ok: false, code: 'bad-params' };
    }
    for (const g of guards) {
      const r = await g(to, from);
      if (r === false) {
        return { ok: false, code: 'guard-denied' };
      }
    }
    if (typeof route.beforeEnter === 'function') {
      const r = await route.beforeEnter(to, from);
      if (r === false) {
        return { ok: false, code: 'guard-denied' };
      }
    }
    await transport.invoke('navigate', { route: to, params });
    if (isReplace) {
      stack[stack.length - 1] = to;
    } else {
      stack.push(to);
    }
    return { ok: true };
  }

  return router;
}

module.exports = { createRoutes, createRouter };
