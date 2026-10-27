'use strict';
// @muse/cli — seven build stages with per-stage outcome/duration.
// Order: configure → typecheck → compile → emit → native-build → package →
// install. `runStages(stageName, ctx)` runs one stage by name, or all seven
// in order when `stageName` is undefined, stopping at the first failure.
// Each result is `{ stage, ok, durationMs, error? }` where `error` carries
// the first failure message plus the location it surfaced at. Stages are
// fail-closed: everything is validated/computed in memory before anything
// is written, so a failure leaves `dist/` untouched. Artefact writes stay
// in `emit` (`dist/ir.json`) and `package` (`dist/tree.mui.bin`), the same
// two files `build.js` has always produced.
const fs = require('node:fs');
const path = require('node:path');
const ir = require('./ir.js');

const STAGES = [
  'configure',
  'typecheck',
  'compile',
  'emit',
  'native-build',
  'package',
  'install',
];

function stageError(message, location) {
  const err = new Error(message);
  err.location = location;
  return err;
}

// Load `.tsx` sources or throw a stage failure naming the app dir as the
// location. Centralizes the no-sources closed gate every source-reading
// stage depends on.
function loadSourcesOrThrow(appDir) {
  let sources;
  try {
    sources = ir.loadSources(appDir);
  } catch (err) {
    const detail = err && err.message ? err.message : String(err);
    throw stageError(`cannot read sources in '${appDir}': ${detail}`, appDir);
  }
  if (sources.length === 0) {
    throw stageError(`no .tsx sources found in '${appDir}'`, appDir);
  }
  return sources;
}

// Lower sources fully in memory; throws without touching `dist/` when the
// tree is empty or cannot be encoded.
function lowerAndEncodeOrThrow(appDir, sources) {
  let tree;
  try {
    tree = ir.lowerToIR(sources);
    if (!tree || tree.version !== 1 || !Array.isArray(tree.roots) || tree.roots.length === 0) {
      throw new Error('lowering produced an empty tree');
    }
    const bin = ir.encodeTree(tree);
    if (!bin || bin.length <= 4) throw new Error('encoding produced an empty blob');
    return { tree, bin };
  } catch (err) {
    const detail = err && err.message ? err.message : String(err);
    throw stageError(`cannot build '${appDir}': ${detail}`, appDir);
  }
}

function assertAppDir(ctx) {
  const appDir = ctx && ctx.appDir;
  let stat = null;
  try {
    stat = fs.statSync(appDir);
  } catch (_) {
    stat = null;
  }
  if (!stat || !stat.isDirectory()) {
    throw stageError(`'${appDir}' is not a directory`, appDir);
  }
}

async function configureStage(ctx) {
  assertAppDir(ctx);
  loadSourcesOrThrow(ctx.appDir);
}

async function typecheckStage(ctx) {
  assertAppDir(ctx);
  loadSourcesOrThrow(ctx.appDir);
}

async function compileStage(ctx) {
  assertAppDir(ctx);
  const sources = loadSourcesOrThrow(ctx.appDir);
  const lowered = lowerAndEncodeOrThrow(ctx.appDir, sources);
  ctx._tree = lowered.tree;
}

async function emitStage(ctx) {
  assertAppDir(ctx);
  const sources = loadSourcesOrThrow(ctx.appDir);
  const lowered = lowerAndEncodeOrThrow(ctx.appDir, sources);
  const distDir = path.join(ctx.appDir, 'dist');
  const irPath = path.join(distDir, 'ir.json');
  try {
    fs.mkdirSync(distDir, { recursive: true });
    fs.writeFileSync(irPath, JSON.stringify(lowered.tree, null, 2) + '\n', 'utf8');
  } catch (err) {
    try {
      fs.rmSync(irPath, { force: true });
    } catch (_) {
      // Best-effort cleanup; the error below is what matters.
    }
    const detail = err && err.message ? err.message : String(err);
    throw stageError(`cannot write artefacts for '${ctx.appDir}': ${detail}`, irPath);
  }
  ctx._tree = lowered.tree;
  ctx._wroteIr = irPath;
}

async function nativeBuildStage(ctx) {
  assertAppDir(ctx);
}

async function packageStage(ctx) {
  assertAppDir(ctx);
  const sources = loadSourcesOrThrow(ctx.appDir);
  const lowered = lowerAndEncodeOrThrow(ctx.appDir, sources);
  const distDir = path.join(ctx.appDir, 'dist');
  const binPath = path.join(distDir, 'tree.mui.bin');
  try {
    fs.mkdirSync(distDir, { recursive: true });
    fs.writeFileSync(binPath, lowered.bin);
  } catch (err) {
    try {
      fs.rmSync(binPath, { force: true });
    } catch (_) {
      // Best-effort cleanup; the error below is what matters.
    }
    const detail = err && err.message ? err.message : String(err);
    throw stageError(`cannot write artefacts for '${ctx.appDir}': ${detail}`, binPath);
  }
  ctx._wroteBin = binPath;
}

async function installStage(ctx) {
  assertAppDir(ctx);
}

const STAGE_IMPLS = {
  configure: configureStage,
  typecheck: typecheckStage,
  compile: compileStage,
  emit: emitStage,
  'native-build': nativeBuildStage,
  package: packageStage,
  install: installStage,
};

// Run one stage by name, or all seven in order when `stageName` is
// undefined, stopping at the first failure. `ctx` is
// `{ config: ValidConfig, appDir: string }`. Never throws for stage
// failures — they come back as `{ ok: false, error }` entries. An unknown
// `stageName` throws an Error naming the value (callers surface it as a
// usage error before any stage runs).
async function runStages(stageName, ctx) {
  if (stageName !== undefined && !Object.prototype.hasOwnProperty.call(STAGE_IMPLS, stageName)) {
    throw new Error(`unknown stage '${stageName}'. Expected one of: ${STAGES.join(', ')}`);
  }
  const list = stageName === undefined ? STAGES.slice() : [stageName];
  const results = [];
  for (const stage of list) {
    const start = Date.now();
    try {
      await STAGE_IMPLS[stage](ctx);
      results.push({ stage, ok: true, durationMs: Date.now() - start });
    } catch (err) {
      const message = err && err.message ? err.message : String(err);
      const location = (err && err.location) || (ctx && ctx.appDir) || '<app>';
      results.push({ stage, ok: false, durationMs: Date.now() - start, error: { message, location } });
      break;
    }
  }
  return results;
}

module.exports = { STAGES, runStages };
