'use strict';
// FR-MUSE-013 connector: one call registers abilities, binds the live turn
// source, and serves call/tools with no server or socket.

const { createRegistry } = require('../abilities');

function readTurnId(control) {
  if (control && typeof control.getTurn === 'function') {
    const turn = control.getTurn();
    if (turn && typeof turn.id === 'string') return turn.id;
    return undefined;
  }
  if (control && typeof control.turnId === 'string') return control.turnId;
  return undefined;
}

function staleId(message) {
  const err = new Error(message);
  err.code = 'stale-id';
  return err;
}

function createMuseMcp(options) {
  const opts = options || {};
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
  const tools = registry.tools();
  return { registry, tools, call, dispose };
}

module.exports = { createMuseMcp };
