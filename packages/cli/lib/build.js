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
const buildLog = require('./build-log.js');

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

// Every buildable project owns its `muse-native.yaml`: the lookup is the
// literal join `<target>/muse-native.yaml` and never falls back to a
// workspace-root (parent-dir) file. Existence is reported by the build
// failure path, not by the resolver itself.
function resolveProjectConfig(target) {
  return path.join(target, 'muse-native.yaml');
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
  // T2 (AC-WORKFLOW-009): missing/unknown target fails closed at configure,
  // before any config resolution, compilation, or `dist/` write. Names the
  // target and the expected path; exit 2 (config-error) via getExitCode.
  // (No-arg `build` with no <appDir> above stays exit 1 usage error.)
  // Returns early when the dir is absent, so the project-config gate below
  // never sees a nonexistent dir.
  let targetStat = null;
  try {
    targetStat = fs.statSync(resolved);
  } catch (_) {
    targetStat = null;
  }
  if (!targetStat || !targetStat.isDirectory()) {
    process.stderr.write(`Error: target '${appDir}' not found. Expected directory at '${resolved}'.\n`);
    process.exitCode = getExitCode('config');
    return;
  }
  // T3 (AC-SAMPLE-035): every buildable project owns its own
  // `muse-native.yaml`. Resolution order when the canonical file is absent:
  // the deprecated alias (`muse.config.ts` / `capo.config.ts`) is still
  // honoured exactly as before T3; a target dir that looks like a project
  // (owns `package.json` or `src/App.tsx`) fails closed naming its own
  // `<target>/muse-native.yaml`; anything else (empty tmp dir, nonexistent
  // path, `--print-config` flows) keeps the pre-T3 behavior. A parent-dir
  // (workspace-root) file never substitutes.
  const projectConfigPath = resolveProjectConfig(resolved);
  let projectStat = null;
  try {
    projectStat = fs.statSync(projectConfigPath);
  } catch (_) {
    projectStat = null;
  }
  if (!projectStat || !projectStat.isFile()) {
    // Canonical file absent: consult the existing deprecated-alias lookup.
    // `readFileLayer` checks the canonical file first, so any file it finds
    // here must be an alias — use it exactly as before T3. A parse error is
    // also an alias hit: fall through and let `resolveConfig` report it.
    let aliasFile = '';
    try {
      const layer = config.readFileLayer(resolved);
      if (layer && layer.file) aliasFile = layer.file;
    } catch (_) {
      aliasFile = 'alias-parse-error';
    }
    if (!aliasFile) {
      let looksLikeProject = false;
      try {
        const dirStat = fs.statSync(resolved);
        if (dirStat && dirStat.isDirectory()) {
          for (const marker of ['package.json', path.join('src', 'App.tsx')]) {
            try {
              const markerStat = fs.statSync(path.join(resolved, marker));
              if (markerStat && markerStat.isFile()) {
                looksLikeProject = true;
                break;
              }
            } catch (_) {
              // Marker absent — try the next one.
            }
          }
        }
      } catch (_) {
        // Target dir itself absent — not a project; keep pre-T3 behavior.
      }
      if (looksLikeProject) {
        process.stderr.write(`Error: missing muse-native.yaml for target '${appDir}' at '${projectConfigPath}'\n`);
        process.exitCode = 1;
        return;
      }
    }
  }
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
  // the same either way. The positional `examples/m3-gallery` target selects
  // the sample (equivalent to `--with-sample`); selection honoring itself
  // is T3, here we only ensure the flag reaches resolution.
  const argv = [appDir].concat(restArgs);
  if (
    (appDir === 'examples/m3-gallery' || resolved.endsWith(path.join('examples', 'm3-gallery'))) &&
    !argv.includes('--with-sample')
  ) {
    argv.push('--with-sample');
  }
  let resolvedConfig;
  try {
    resolvedConfig = config.resolveConfig(argv, process.env, resolved);
  } catch (err) {
    const message = err && err.message ? err.message : String(err);
    process.stderr.write(message.endsWith('\n') ? message : message + '\n');
    process.exitCode = getExitCode('config');
    return;
  }
  // BUG-TSX-010 T2: validate the `muse:` section fail-closed before any
  // compile/stage work. `readFileLayer` re-reads the same app dir
  // `resolveConfig` just resolved (T1 routes `muse:` out of the scalar map
  // and exposes the parsed subtree here); an absent `muse:` section returns
  // ok and builds as before. A present section is checked against the proxy
  // allow-list; an unavailable allow-list fails closed, same as any other
  // config error (stderr + exit 2, no `dist/` writes).
  try {
    const { validateMuseSection, ConfigError: MuseConfigError } = require('./muse-models.js');
    const fileLayer = config.readFileLayer(resolved);
    if (fileLayer.muse !== undefined) {
      let allowList;
      try {
        const { loadConfig } = require('../../../proxy/lib/load-config.js');
        allowList = loadConfig(path.join(__dirname, '..', '..', '..', 'proxy', 'proxy.config.json'));
      } catch (_) {
        throw new MuseConfigError('muse', 'muse-native.yaml', 'a readable proxy allow-list');
      }
      validateMuseSection(fileLayer.muse, allowList);
    }
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
  // T3 (AC-WORKFLOW-007): a selected sample target must exist before any
  // compilation or `dist/` write. Fails closed with exit 2 (config-error),
  // naming the target and the expected path. `--print-config` above stays
  // a pure resolution print and never fails on an absent sample.
  if (stages.isSampleSelected(resolvedConfig.config)) {
    const sampleDir = stages.resolveSampleDir(resolved);
    let sampleStat = null;
    try {
      sampleStat = fs.statSync(sampleDir);
    } catch (_) {
      sampleStat = null;
    }
    if (!sampleStat || !sampleStat.isDirectory()) {
      process.stderr.write(
        `Error: sample target 'examples/m3-gallery' not found. Expected directory at '${sampleDir}'.\n`,
      );
      process.exitCode = getExitCode('config');
      return;
    }
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
module.exports.resolveProjectConfig = resolveProjectConfig;
// T2 (FR-TSX-009): the combined tsc+AOT build-log surface lives in
// `./build-log.js`; the build flow owns any file writing, so the pure
// string helpers are exposed here without changing existing behavior.
module.exports.appendVerdict = buildLog.appendVerdict;
module.exports.runBothValidators = buildLog.runBothValidators;
