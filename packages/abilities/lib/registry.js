'use strict';
// Capability-gated fail-closed registry with turnId-stamped bridge.
// Mirrors the stale-commit rule of the intelligence turn helper: turn ids
// carry the mint shape `turn-<seq>-<rand>` and the registry keeps a
// high-water mark — an older seq calling after a newer seq committed is
// stale (registry code `stale-id`).

function fail(code, message) {
  const err = new Error(message);
  err.code = code;
  throw err;
}

function parseSeq(turnId) {
  const m = /^turn-(\d+)-/.exec(turnId);
  if (!m) return null;
  return Number.parseInt(m[1], 10);
}

function createRegistry(options) {
  const granted = new Set(
    options && Array.isArray(options.capabilities) ? options.capabilities : []
  );
  const byName = new Map();
  let highWater = 0;

  function register(ability) {
    byName.set(ability.name, ability);
  }

  function call(name, args, ctx) {
    // Precedence: missing turnId (stale-id) > high-water staleness
    // (stale-id) > unknown-ability > capability-denied. A stale turn with
    // an unknown name therefore reports stale-id.
    const turnId = ctx && ctx.turnId;
    if (typeof turnId !== 'string' || turnId.length === 0) {
      fail('stale-id', `call "${name}" rejected: missing turn id`);
    }
    const seq = parseSeq(turnId);
    if (seq !== null && seq < highWater) {
      fail('stale-id', `stale turn for "${name}": ${turnId}`);
    }
    const ability = byName.get(name);
    if (!ability) {
      fail('unknown-ability', `unknown ability: ${name}`);
    }
    const required = Array.isArray(ability.capabilities) ? ability.capabilities : [];
    for (const cap of required) {
      if (!granted.has(cap)) {
        fail('capability-denied', `capability denied for "${name}": ${cap}`);
      }
    }
    if (seq !== null && seq > highWater) {
      highWater = seq;
    }
    return ability.invoke(args === undefined ? {} : args, { turnId });
  }

  function tools() {
    return Array.from(byName.values()).map((ability) => ({
      name: ability.name,
      args: Object.assign({}, ability.args),
      capabilities: Array.isArray(ability.capabilities)
        ? ability.capabilities.slice()
        : [],
    }));
  }

  return { register, call, tools };
}

module.exports = { createRegistry };
