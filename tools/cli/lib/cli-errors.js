'use strict';
// T5 — FR-DX-008 / AC-DX-036 / TC-DX-036.
// User-facing CLI error presentation: every failure names the command, the
// likely cause (preserved verbatim), and a next action. Never requires
// engine source and never leaks raw stacks.

function formatFailure(failure) {
  if (!failure || typeof failure !== 'object') {
    throw new TypeError('formatFailure requires {command, cause}');
  }
  const { command, cause } = failure;
  if (typeof command !== 'string' || command.length === 0) {
    throw new TypeError('formatFailure requires a non-empty command string');
  }
  if (typeof cause !== 'string' || cause.length === 0) {
    throw new TypeError('formatFailure requires a non-empty cause string');
  }
  return {
    command,
    cause,
    action: `Fix '${cause}' for '${command}', then re-run the command.`,
  };
}

module.exports = { formatFailure };
