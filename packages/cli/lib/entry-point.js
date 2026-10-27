'use strict';
// @muse/cli — single entry point registry (NFR-BUILD-003 / AC-BUILD-003).
// Maps each NFR-BUILD-003 operation onto its CLI command served through
// `packages/cli/lib/cli.js` dispatch (read-only; this module never changes
// it). `create` is served by the existing `init` alias; `build`/`run` map
// directly; `test`/`profile`/`package` resolve to their CLI commands
// (explicit `Error('not yet implemented')` stubs owned by NFR-BUILD-005
// still route through the CLI, never bypass it).

const REGISTRY = [
  { operation: 'create', command: 'init' },
  { operation: 'build', command: 'build' },
  { operation: 'run', command: 'run' },
  { operation: 'test', command: 'test' },
  { operation: 'profile', command: 'profile' },
  { operation: 'package', command: 'package' },
];

// Zero-arg returns the full registry array (fresh copies); single string
// arg returns the matching entry's command, throwing
// `Error('unknown operation: <name>')` for unknown names.
function getEntryPointCommands(operation) {
  if (typeof operation === 'undefined') {
    return REGISTRY.map((entry) => ({ operation: entry.operation, command: entry.command }));
  }
  const entry = REGISTRY.find((e) => e.operation === operation);
  if (!entry) {
    throw new Error(`unknown operation: ${operation}`);
  }
  return entry.command;
}

module.exports = { getEntryPointCommands };
