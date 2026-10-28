'use strict';
// T5 — FR-DX-008 / AC-DX-036.
// In-memory plugin registry: plugins add commands without forking the CLI.
// NOTE: `registry` is a module-global singleton (no unregister/clear by
// design); long-lived hosts share it across sessions.

const registry = new Map();

function registerPlugin(plugin) {
  if (!plugin || typeof plugin !== 'object') {
    throw new TypeError('registerPlugin requires {name, command, run}');
  }
  const { name, command, run } = plugin;
  if (typeof name !== 'string' || name.length === 0) {
    throw new TypeError('registerPlugin requires a non-empty name string');
  }
  if (typeof command !== 'string' || command.length === 0) {
    throw new TypeError('registerPlugin requires a non-empty command string');
  }
  if (typeof run !== 'function') {
    throw new TypeError('registerPlugin requires a run function');
  }
  if (registry.has(command)) {
    throw new Error(`command '${command}' is already registered (duplicate plugin command)`);
  }
  registry.set(command, { name, command, run });
  return { name, command };
}

function getPlugin(command) {
  return registry.get(command);
}

function listCommands() {
  return [...registry.keys()];
}

function dispatch(command, args) {
  const plugin = registry.get(command);
  if (!plugin) {
    throw new Error(`unknown command '${command}'`);
  }
  return plugin.run(Array.isArray(args) ? args : []);
}

module.exports = { registerPlugin, getPlugin, listCommands, dispatch };
