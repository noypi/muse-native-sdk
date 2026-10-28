'use strict';
// ProxyError taxonomy for the Muse proxy dispatch surface.
// Only documented codes may surface from dispatch/stub.

const DOCUMENTED_CODES = new Set([
  'auth',
  'upstream',
  'timeout',
  'schema',
  'unknown-route',
  'unlisted-param',
  'unknown-preset',
  'non-muse-model',
]);

class ProxyError extends Error {
  // code: one of DOCUMENTED_CODES; route: offending route; retryable: boolean.
  constructor(code, route, retryable, message) {
    super(message || `${code}: ${route}`);
    this.name = 'ProxyError';
    this.code = code;
    this.route = route;
    this.retryable = retryable === true;
  }
}

module.exports = { ProxyError, DOCUMENTED_CODES };
