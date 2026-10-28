'use strict';
// T4 — FR-DX-008 / AC-DX-035 / TC-DX-035.
// Deep-link tester: dispatches a route opaquely through nav resolution.
// Unknown / empty / malformed routes yield a structured symbolized route
// error — never a raw stack.

let navResolver = (route) => {
  if (typeof route === 'string' && route.length > 0) return { ok: true };
  return { ok: false, symbol: 'NAV_EMPTY_ROUTE', hint: 'provide a valid deep link route' };
};

function __setNavResolver(fn) {
  if (typeof fn === 'function') navResolver = fn;
}

function routeError(route, symbol, detail, action) {
  return {
    status: 'routeError',
    route: String(route),
    cause: `unknown route '${String(route)}' [${symbol} AC-DX-035]${detail ? `: ${detail}` : ''}`,
    action: String(action),
  };
}

function dispatchRoute(route, opts) {
  void (opts && opts.coldStart);
  try {
    if (typeof route !== 'string' || route.length === 0) {
      return {
        status: 'routeError',
        route: String(route),
        cause: `unknown route '${String(route)}' [NAV_EMPTY_ROUTE AC-DX-035]: empty route`,
        action: 'Provide a valid deep link route (e.g. muse://home) and retry. [AC-DX-035]',
      };
    }
    if (!route.includes('://')) {
      return routeError(route, 'NAV_UNKNOWN_ROUTE', 'malformed route, missing scheme', 'Check route spelling and scheme (e.g. muse://home), then retry. [AC-DX-035]');
    }
    let verdict;
    try {
      verdict = navResolver(route);
    } catch (err) {
      const detail = err && err.message ? String(err.message).split('\n')[0] : String(err).split('\n')[0];
      return routeError(route, 'NAV_UNKNOWN_ROUTE', detail, 'Check route spelling and scheme (e.g. muse://home), then retry. [AC-DX-035]');
    }
    if (verdict && verdict.ok) {
      return { status: 'resolved', route };
    }
    const symbol =
      verdict && typeof verdict.symbol === 'string' && verdict.symbol
        ? verdict.symbol
        : 'NAV_UNKNOWN_ROUTE';
    const hint =
      verdict && typeof verdict.hint === 'string' && verdict.hint
        ? verdict.hint
        : 'check route spelling';
    return routeError(route, symbol, hint, 'Check route spelling and scheme (e.g. muse://home), then retry. [AC-DX-035]');
  } catch (err) {
    const detail = err && err.message ? String(err.message).split('\n')[0] : 'unexpected dispatch failure';
    return routeError(route, 'NAV_UNKNOWN_ROUTE', detail, 'Check route spelling and scheme (e.g. muse://home), then retry. [AC-DX-035]');
  }
}

module.exports = { dispatchRoute, __setNavResolver };
