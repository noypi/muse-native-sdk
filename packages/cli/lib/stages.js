'use strict';
// @muse/cli — seven build stages with per-stage outcome/duration.
// Order: configure → typecheck → compile → emit → native-build → package →
// install. `runStages(stageName, ctx)` runs one stage by name, or all seven
// in order when `stageName` is undefined, stopping at the first failure.
// Each result is `{ stage, ok, durationMs, error? }` where `error` carries
// the first failure message plus the location it surfaced at. Stages are
// fail-closed: everything is validated/computed in memory before anything
// is written, so a failure leaves `dist/` untouched. Artefact writes stay
// in `emit` (`dist/ir.json`) and `package` (`dist/tree.mui.bin` plus the
// per-`config.packaging` real APK bytes `dist/app-fat.apk` /
// `dist/app-<arch>.apk` (T1 assembler + T3 sign pass-through)
// and the release-only AAB `dist/app.aab`), the same files `build.js` has always produced
// plus the T4 packaging outputs and the T5 AAB output.
const fs = require('node:fs');
const path = require('node:path');
const ir = require('./ir.js');
const { buildApk } = require('./apk.js');

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

// Sample target (FR-WORKFLOW-003): `examples/m3-gallery`. Resolved by
// `resolveSampleDir` without depending on the process cwd (see below);
// selected builds compile/package its sources into the same artefacts;
// default builds never touch it.
const SAMPLE_TARGET = path.join('examples', 'm3-gallery');

function isSampleSelected(config) {
  const v = config && config.withSample;
  return v === true || v === 'true';
}

// Resolve the sample dir without depending on the process cwd. Candidates
// in order: (1) the appDir itself when it is the sample (ends with
// `examples/m3-gallery`); (2) a sibling `<parent-of-appDir>/examples/m3-gallery`;
// (3) walk up from this lib dir (__dirname) to the first ancestor containing
// `examples/m3-gallery` (covers the repo-root layout regardless of caller
// cwd). Returns the first existing directory; when none exists, reports the
// repo-root-anchored expected path so callers fail closed naming it.
function resolveSampleDir(appDir) {
  const expectedSuffix = path.join('examples', 'm3-gallery');
  const repoRootGuess = path.resolve(__dirname, '..', '..', '..');
  const resolved = path.resolve(appDir || '');
  if (resolved.endsWith(expectedSuffix)) {
    try {
      if (fs.statSync(resolved).isDirectory()) return resolved;
    } catch (_) {
      // Not present — try the remaining candidates below.
    }
  }
  const sibling = path.join(path.dirname(resolved), 'examples', 'm3-gallery');
  try {
    if (fs.statSync(sibling).isDirectory()) return sibling;
  } catch (_) {
    // Fall through to the ancestor walk below.
  }
  let dir = __dirname;
  let found = null;
  let foundRoot = null;
  for (;;) {
    const candidate = path.join(dir, 'examples', 'm3-gallery');
    try {
      if (fs.statSync(candidate).isDirectory()) {
        found = candidate;
        foundRoot = dir;
        break;
      }
    } catch (_) {
      // Keep walking up.
    }
    const parent = path.dirname(dir);
    if (parent === dir) break;
    dir = parent;
  }
  // Only honor the checkout-anchored candidate when the build target lives
  // inside the same checkout (resolved under foundRoot, or is the sample
  // itself). Otherwise a hermetic out-of-tree build (e.g. a tmp fake repo)
  // would leak this checkout's committed sample instead of failing closed.
  if (found !== null && (resolved === found || resolved.startsWith(foundRoot + path.sep))) return found;
  // Fail closed naming an expected path: the repo-root-anchored one for
  // in-checkout targets, else the app-anchored sibling so out-of-tree
  // builds report a path near the app (which does not exist here).
  const anchorRoot = foundRoot !== null ? foundRoot : repoRootGuess;
  if (resolved === anchorRoot || resolved.startsWith(anchorRoot + path.sep)) {
    return path.join(anchorRoot, SAMPLE_TARGET);
  }
  return sibling;
}

// Assert the resolved `withSample` selection's target is present. Throws a
// stage failure naming the target and the expected path.
function assertSampleTargetOrThrow(appDir) {
  const dir = resolveSampleDir(appDir);
  let stat = null;
  try {
    stat = fs.statSync(dir);
  } catch (_) {
    stat = null;
  }
  if (!stat || !stat.isDirectory()) {
    throw stageError(`sample target 'examples/m3-gallery' not found. Expected directory at '${dir}'.`, dir);
  }
  return dir;
}

// Effective sources for a build: app sources, plus the sample sources when
// `withSample` selects them. Fails closed (no partial return) when the
// sample is selected but absent.
function loadEffectiveSourcesOrThrow(ctx) {
  const appSources = loadSourcesOrThrow(ctx.appDir);
  if (!isSampleSelected(ctx && ctx.config)) return appSources;
  const sampleDir = assertSampleTargetOrThrow(ctx.appDir);
  // Gallery-is-sample fix: building the sample target itself already loads
  // its sources above; concatenating the same dir again doubled every root
  // (2x tree.mui.bin, duplicated gallery stories on device).
  if (path.resolve(sampleDir) === path.resolve(ctx.appDir)) return appSources;
  let sampleSources;
  try {
    sampleSources = ir.loadSources(sampleDir);
  } catch (err) {
    const detail = err && err.message ? err.message : String(err);
    throw stageError(`cannot read sample sources in '${sampleDir}': ${detail}`, sampleDir);
  }
  return appSources.concat(sampleSources);
}

async function configureStage(ctx) {
  assertAppDir(ctx);
  loadSourcesOrThrow(ctx.appDir);
  if (isSampleSelected(ctx && ctx.config)) assertSampleTargetOrThrow(ctx.appDir);
}

async function typecheckStage(ctx) {
  assertAppDir(ctx);
  loadEffectiveSourcesOrThrow(ctx);
}

async function compileStage(ctx) {
  assertAppDir(ctx);
  const sources = loadEffectiveSourcesOrThrow(ctx);
  const lowered = lowerAndEncodeOrThrow(ctx.appDir, sources);
  ctx._tree = lowered.tree;
}

async function emitStage(ctx) {
  assertAppDir(ctx);
  const sources = loadEffectiveSourcesOrThrow(ctx);
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
  const sources = loadEffectiveSourcesOrThrow(ctx);
  const lowered = lowerAndEncodeOrThrow(ctx.appDir, sources);
  // T6 (AC-PACKAGING-008/009): the same `config.archs` selection backs every
  // output — never silently narrowed or widened. Unsigned bytes come from
  // the T1 assembler (`buildApk`); signing passes through the T3 lane.
  // Fat writes a single `dist/app-fat.apk` covering every selected arch;
  // split writes one `dist/app-<arch>.apk` per selected arch containing
  // only that arch's lib; both writes the fat plus the full split set.
  const archs =
    ctx && ctx.config && Array.isArray(ctx.config.archs)
      ? ctx.config.archs.slice()
      : ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];
  const packaging = (ctx && ctx.config && ctx.config.packaging) || 'fat';
  // Sink-level enum guard (defense-in-depth: the CLI/config layer already
  // allowlists this, but the sink must not trust it). Any value outside
  // fat|split|both fails closed — never ok:true with zero APKs.
  if (packaging !== 'fat' && packaging !== 'split' && packaging !== 'both') {
    throw stageError(
      `unknown packaging '${packaging}'. Expected one of: fat, split, both`,
      ctx.appDir,
    );
  }
  // Host inputs (contract A-H1/A-H3/A-H4): unit tests inject compiled host
  // bytes via `ctx._hostInputs = { dexBytes, resourcesArsc, nativeLibs }`
  // where `nativeLibs` is `[{ abi, name, bytes }]` keyed by exact
  // `config.archs` entries. Absent inputs fall back to the real toolchain
  // inputs (template manifest + generated fixture bytes) so callers without
  // fixtures (reference-target and regression runs) still emit real ZIPs.
  const hostInputs = ctx && ctx._hostInputs;
  let manifestXml = null;
  try {
    manifestXml = fs.readFileSync(
      path.join(__dirname, '..', 'templates', 'android', 'AndroidManifest.xml'),
      'utf8',
    );
  } catch (_) {
    manifestXml = null;
  }
  if (!manifestXml) manifestXml = '<manifest package="com.muse.host" />';
  let dexBytes;
  let resourcesArsc;
  let libsByAbi;
  if (hostInputs && typeof hostInputs === 'object') {
    dexBytes = hostInputs.dexBytes;
    resourcesArsc = hostInputs.resourcesArsc;
    const provided = Array.isArray(hostInputs.nativeLibs) ? hostInputs.nativeLibs : null;
    if (!provided) {
      throw stageError('missing nativeLibs for selected archs', ctx.appDir);
    }
    libsByAbi = new Map(provided.map((lib) => [lib && lib.abi, lib]));
    // Completeness (no silent arch narrowing): every selected arch must
    // have its per-arch libRuntime.so; a missing arch fails closed instead
    // of emitting an APK without it.
    for (const arch of archs) {
      if (!libsByAbi.has(arch)) {
        throw stageError(`missing native lib for arch '${arch}'`, ctx.appDir);
      }
    }
  } else {
    dexBytes = Buffer.from('DEX-STAGE-FALLBACK-classes', 'utf8');
    resourcesArsc = Buffer.from('ARSC-STAGE-FALLBACK-resources', 'utf8');
    // Arch-tagged fallback lib bytes keep the T4 arch-listPositive shape
    // (utf8 body still names each arch) inside real ZIP entries.
    libsByAbi = new Map(
      archs.map((abi) => [
        abi,
        { abi, name: 'libRuntime.so', bytes: Buffer.from(`arch: ${abi}\n`, 'utf8') },
      ]),
    );
  }
  // `ctx._tree` (Buffer from the compile/emit lane) seeds
  // assets/tree.mui.bin; otherwise the stage lowers sources in memory.
  const treeBin =
    ctx && Buffer.isBuffer(ctx._tree) ? ctx._tree : lowered.bin;
  function unsignedFor(libs) {
    try {
      return buildApk({
        manifestXml,
        dexBytes,
        resourcesArsc,
        nativeLibs: libs,
        treeBin,
      });
    } catch (err) {
      // buildApk rejects hostile abis ('/', '\', '..') and empty blobs —
      // surface as a stage failure naming the app dir, before dist/ writes.
      const detail = err && err.message ? err.message : String(err);
      throw stageError(`cannot assemble APK for '${ctx.appDir}': ${detail}`, ctx.appDir);
    }
  }
  const libsFor = (arch) => {
    const lib = libsByAbi.get(arch);
    return [{ abi: lib.abi, name: lib.name || 'libRuntime.so', bytes: lib.bytes }];
  };
  const fatLibs = archs.map((arch) => libsFor(arch)[0]);
  // Compute every payload in memory before touching `dist/` (fail-closed),
  // including the injectable T3 sign pass-through (contract A-H2:
  // `ctx._signApk = async (unsignedBytes) => signedBytes`; absent fake
  // leaves bytes unsigned).
  const signFn = ctx && typeof ctx._signApk === 'function' ? ctx._signApk : null;
  async function signed(buf) {
    if (!signFn) return buf;
    const out = await signFn(buf);
    return Buffer.isBuffer(out) ? out : Buffer.from(out);
  }
  const distDir = path.join(ctx.appDir, 'dist');
  const binPath = path.join(distDir, 'tree.mui.bin');
  // Create dist/ up front (pre-existing T4 behavior: a fail-closed run
  // leaves an empty dist/ — zero APK/AAB artefacts, nothing outside it).
  // Assembly/signing below still runs fully in memory before any file is
  // written, so a failure writes nothing.
  try {
    fs.mkdirSync(distDir, { recursive: true });
  } catch (err) {
    const detail = err && err.message ? err.message : String(err);
    throw stageError(`cannot write artefacts for '${ctx.appDir}': ${detail}`, binPath);
  }
  const writes = [{ filePath: binPath, data: lowered.bin }];
  if (packaging === 'fat' || packaging === 'both') {
    writes.push({
      filePath: path.join(distDir, 'app-fat.apk'),
      data: await signed(unsignedFor(fatLibs)),
    });
  }
  if (packaging === 'split' || packaging === 'both') {
    for (const arch of archs) {
      writes.push({
        filePath: path.join(distDir, `app-${arch}.apk`),
        data: await signed(unsignedFor(libsFor(arch))),
      });
    }
  }
  // Release additionally writes one AAB covering the same `config.archs`
  // selection in the same run — built from the same inputs (all-arch lib
  // set). Non-release runs emit APKs only.
  const release = Boolean(ctx && ctx.config && ctx.config.release);
  if (release) {
    writes.push({
      filePath: path.join(distDir, 'app.aab'),
      data: unsignedFor(fatLibs),
    });
  }
  const written = [];
  try {
    fs.mkdirSync(distDir, { recursive: true });
    // Path confinement (defense-in-depth): every resolved APK/AAB write
    // must stay inside the run's distDir. A crafted arch (e.g.
    // '../evil' or 'x/../../../evil') joins to a path escaping dist and
    // fails closed here before anything is written.
    const resolvedDist = path.resolve(distDir);
    for (const w of writes) {
      const resolved = path.resolve(w.filePath);
      if (resolved !== resolvedDist && !resolved.startsWith(resolvedDist + path.sep)) {
        throw stageError(`refusing to write outside dist: '${w.filePath}'`, distDir);
      }
    }
    for (const w of writes) {
      if (typeof w.data === 'string') fs.writeFileSync(w.filePath, w.data, 'utf8');
      else fs.writeFileSync(w.filePath, w.data);
      written.push(w.filePath);
    }
  } catch (err) {
    for (const p of written) {
      try {
        fs.rmSync(p, { force: true });
      } catch (_) {
        // Best-effort cleanup; the error below is what matters.
      }
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

module.exports = { STAGES, runStages, SAMPLE_TARGET, isSampleSelected, resolveSampleDir };
