'use strict';
// T2 (FR-LIFECYCLE-003) — in-place configuration handling with no recreation.
// Maps a config `change` verbatim to the T1 `config:<change>` self-loop and
// dispatches it through the T2 observer hub. The machine state is unchanged
// by construction; `recreated` is always `false`. Unknown change strings
// propagate the hub/machine throw.

function createConfigManager(hub) {
  function onConfigurationChanged(change) {
    const result = hub.dispatch(`config:${change}`);
    return { applied: result.applied, state: result.state, recreated: false };
  }

  return { onConfigurationChanged };
}

module.exports = { createConfigManager };
