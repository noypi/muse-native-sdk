'use strict';
// @muse/cli — `muse-native build <appDir>` emits the UI IR + binary UI tree.
// Single source tree in, two artefacts out: `<appDir>/dist/ir.json` and
// `<appDir>/dist/tree.mui.bin`. Fails closed: missing dir / no `.tsx`
// sources is an error + exit 1 with no partial `dist/` writes (everything
// is validated in memory before anything is written).
const fs = require('node:fs');
const path = require('node:path');
const config = require('./config.js');
const stages = require('./stages.js');

// Pull `--stage <name>` out of the flags after <appDir>. Returns
// `{ stageName, rest }` where `rest` keeps every other flag untouched so
// config resolution never sees `--stage` (it is not a config key).
function splitStageFlag(rest) {
  const args = Array.isArray(rest) ? rest.slice() : [];
  const at = args.indexOf('--stage');
  if (at === -1) return { stageName: undefined, rest: args };
  const value = args[at + 1];
  if (typeof value !== 'string' || value.startsWith('--')) {
    const err = new Error('Error: --stage requires a value. Expected one of: ' + stages.STAGES.join(', '));
    err.code = 'STAGE_USAGE';
    throw err;
  }
  args.splice(at, 2);
  return { stageName: value, rest: args };
}

function runBuild(appDir, rest) {
  if (!appDir || appDir.startsWith('--')) {
    process.stderr.write('Error: missing <appDir>.\nUsage: muse-native build <appDir>\n');
    process.exitCode = 1;
    return;
  }
  const { getExitCode } = require('./exit-codes.js');
  const { validateOptions } = require('./cli.js');
  // Option validation runs at `configure`, before any compilation: unknown
  // options, invalid values and incompatible combinations exit 2 naming
  // the option and the accepted values, with no `dist/` writes.
  try {
    validateOptions(['build', appDir].concat(Array.isArray(rest) ? rest : []));
  } catch (err) {
    const message = err && err.message ? err.message : String(err);
    process.stderr.write(message.endsWith('\n') ? message : message + '\n');
    process.exitCode = getExitCode('config');
    return;
  }
  const resolved = path.resolve(appDir);
  // `--stage <name>` runs one stage only; anything else after <appDir>
  // stays flags for config resolution. Unknown stage values fail here,
  // before any stage runs, naming the bad value.
  let stageName;
  let restArgs;
  try {
    const split = splitStageFlag(rest);
    stageName = split.stageName;
    restArgs = split.rest;
  } catch (err) {
    const message = err && err.message ? err.message : String(err);
    process.stderr.write(message.endsWith('\n') ? message : message + '\n');
    process.exitCode = getExitCode('config');
    return;
  }
  if (stageName !== undefined && !stages.STAGES.includes(stageName)) {
    process.stderr.write(`Error: unknown stage '${stageName}'. Expected one of: ${stages.STAGES.join(', ')}\n`);
    process.exitCode = getExitCode('config');
    return;
  }
  // Resolve typed config first: failures are fail-closed before any
  // compilation or artefact writes (no partial config, no dist/ writes).
  // `restArgs` holds the flags after <appDir> minus `--stage`; argv for
  // resolveConfig keeps the positional so `--<key> <value>` pairs parse
  // the same either way.
  const argv = [appDir].concat(restArgs);
  let resolvedConfig;
  try {
    resolvedConfig = config.resolveConfig(argv, process.env, resolved);
  } catch (err) {
    const message = err && err.message ? err.message : String(err);
    process.stderr.write(message.endsWith('\n') ? message : message + '\n');
    process.exitCode = getExitCode('config');
    return;
  }
  if (argv.includes('--print-config')) {
    process.stdout.write(JSON.stringify(resolvedConfig, null, 2) + '\n');
    process.exitCode = getExitCode('ok');
    return;
  }
  let stat = null;
  try {
    stat = fs.statSync(resolved);
  } catch (err) {
    process.stderr.write(`Error: directory '${appDir}' does not exist.\n`);
    process.exitCode = 1;
    return;
  }
  if (!stat.isDirectory()) {
    process.stderr.write(`Error: '${appDir}' is not a directory.\n`);
    process.exitCode = 1;
    return;
  }
  // Dispatch through the seven stages (or the one named by `--stage`).
  // Per-stage outcome lines go to stdout; a failure also mirrors the error
  // to stderr, stops the pipeline, and exits nonzero with no partial
  // `dist/` writes (stages validate in memory before writing).
  const ctx = { config: resolvedConfig.config, appDir: resolved };
  Promise.resolve()
    .then(() => stages.runStages(stageName, ctx))
    .then((results) => {
      let failed = false;
      for (const result of results) {
        if (result.ok) {
          process.stdout.write(`${result.stage}: ok (${result.durationMs}ms)\n`);
        } else {
          failed = true;
          const message = result.error && result.error.message ? result.error.message : 'unknown error';
          const location = result.error && result.error.location ? result.error.location : resolved;
          process.stdout.write(`${result.stage}: FAIL ${message} at ${location}\n`);
          process.stderr.write(`Error: ${message}\n`);
        }
      }
      if (failed) {
        process.exitCode = getExitCode('compile');
        return;
      }
      // Name the artefacts the run wrote, as the build has always done.
      if (ctx._wroteIr) process.stdout.write(`Wrote ${ctx._wroteIr}\n`);
      if (ctx._wroteBin) process.stdout.write(`Wrote ${ctx._wroteBin}\n`);
      process.exitCode = getExitCode('ok');
    })
    .catch((err) => {
      const message = err && err.message ? err.message : String(err);
      process.stderr.write(message.endsWith('\n') ? message : message + '\n');
      process.exitCode = getExitCode('compile');
    });
}

module.exports = runBuild;
