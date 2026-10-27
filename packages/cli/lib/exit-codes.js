'use strict';
// @muse/cli — distinct process exit codes for `muse-native build`.
// 0 ok (success), 2 config/usage error (bad option, unknown key, wrong
// type), 1 compile/stage failure, 3 packaging failure. The packaging path
// is stubbed: no live packager exists yet (FR-BUILD owns it), so only the
// mapping function is asserted, not an end-to-end packaging run.
function getExitCode(kind) {
  switch (kind) {
    case 'ok':
      return 0;
    case 'config':
      return 2;
    case 'compile':
      return 1;
    case 'package':
      return 3;
    default:
      throw new Error(`Error: unknown exit-code kind '${kind}'. Expected one of: ok, config, compile, package`);
  }
}

module.exports = { getExitCode };
