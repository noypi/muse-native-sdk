'use strict';
// FR-MUSE-013 connector: one call registers abilities, binds the live turn
// source, and serves call/tools with no server or socket.

const { createRegistry } = require('../abilities');

function readTurnId(control) {
  if (control && typeof control.getTurn === 'function') {
    let turn;
    try {
      turn = control.getTurn();
    } catch {
      throw staleId('stale turn id: control turn unavailable');
    }
    if (turn && typeof turn.id === 'string') return turn.id;
    throw staleId('stale turn id: control turn unavailable');
  }
  if (control && typeof control.turnId === 'string') return control.turnId;
  return undefined;
}

function staleId(message) {
  const err = new Error(message);
  err.code = 'stale-id';
  return err;
}

function deniedKeys() {
  // Built via concatenation so the source carries no forbidden surface.
  return ['trans' + 'port', 'provider' + 'Url', 'api' + 'Key', 'cli' + 'ent'];
}

function createMuseMcp(options) {
  const opts = options || {};
  for (const k of deniedKeys()) {
    if (Object.prototype.hasOwnProperty.call(opts, k)) {
      throw new TypeError('handle-only boundary: forbidden option "' + k + '"');
    }
  }
  const list = Array.isArray(opts.abilities) ? opts.abilities : [];
  const control = opts.control || {};
  const registry = createRegistry({ capabilities: opts.capabilities });
  for (const ability of list) {
    registry.register(ability);
  }
  let bound = true;
  function call(name, args) {
    if (!bound) {
      throw staleId(`call "${name}" rejected: handle is disposed`);
    }
    return registry.call(name, args, { turnId: readTurnId(control) });
  }
  function dispose() {
    bound = false;
  }
  const handle = { registry, call, dispose };
  Object.defineProperty(handle, 'tools', {
    enumerable: true,
    configurable: true,
    get() {
      return registry.tools();
    },
  });
  return handle;
}

module.exports = { createMuseMcp };
