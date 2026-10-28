'use strict';
// T2 — stub proxy: serves recorded SAM/Spark fixtures with no network and no key.
// Synchronous handle(req): { status, body }; throws a documented
// ProxyError (unknown-route) for unknown routes.

const { ProxyError } = require('./errors');

const FIXTURES = {
  segment: { mask: 'polygon', encoding: 'polygon' },
  reason: { reasoning: 'recorded-spark-reason', chunks: ['{"t":"rec"}'] },
  structure: { structure: 'recorded-spark-structure', strict: true },
  transcribe: { text: 'recorded-transcript', chunks: ['a'] },
  'generate-image': { image: 'recorded-image-bytes', size: '1024x1024' },
};

function clone(value) {
  return JSON.parse(JSON.stringify(value));
}

function handle(req) {
  const route = req && typeof req === 'object' ? req.route : undefined;
  if (typeof route !== 'string' || !Object.prototype.hasOwnProperty.call(FIXTURES, route)) {
    throw new ProxyError('unknown-route', String(route), false);
  }
  return { status: 200, body: clone(FIXTURES[route]) };
}

module.exports = { handle };
