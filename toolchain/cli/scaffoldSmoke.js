'use strict';
// T6 — Full smoke composition: scaffold → signed AAB → mount → symbols.
// Thin composition over T1 (packages/cli/lib/init.js scaffoldProject), T2
// (toolchain/cli/buildSignedAab.js), T3 (toolchain/cli/playDelivery.js), and
// T4 (toolchain/cli/symbolArtifacts.js). No stage logic is reimplemented here:
// stage internals (signing, retention, delivery progress) belong to T1–T4 and
// are covered by their own contracts. This module only wires the observable
// outcome and names the failing stage as `smoke-failed: <stage>: <reason>`.
const fs = require('node:fs');
const path = require('node:path');

const { scaffoldProject } = require('../../packages/cli/lib/init.js');
const { buildSignedAab } = require('./buildSignedAab.js');
const { requestPack, monitorPack, mountPack } = require('./playDelivery.js');
const { collectSymbols } = require('./symbolArtifacts.js');

const SMOKE_KEY_ALIAS = 'fake-alias-not-real';

function stripStagePrefix(message) {
  const idx = message.indexOf(':');
  if (idx === -1) return message.trim() || 'failed';
  const rest = message.slice(idx + 1).trim();
  return rest.length > 0 ? rest : message;
}

function smokeFailed(stage, err) {
  const reason = err instanceof Error ? stripStagePrefix(err.message) : String(err);
  throw new Error(`smoke-failed: ${stage}: ${reason}`);
}

function ensureBuildInputs(projectDir) {
  // Bridge glue, not stage logic: the T1 scaffold emits the manifest plus
  // placeholder bundle inputs (package.json + src/App.tsx), while T2 consumes
  // stub bundle artifacts. Materialize the T2 stubs deterministically when the
  // scaffold did not provide them so the composed stages meet hermetically.
  const stubs = {
    'bundle.stub.js': "'use strict';\n",
    'ui-tree.stub.json': '{"tree":"fake-not-real"}\n',
  };
  for (const [rel, content] of Object.entries(stubs)) {
    const full = path.join(projectDir, rel);
    let stat = null;
    try {
      stat = fs.statSync(full);
    } catch {
      stat = null;
    }
    if (stat && stat.isFile()) continue;
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, content, 'utf8');
  }
}

function ensureSymbolInputs(projectDir) {
  // Bridge glue, not stage logic: T4 consumes unstripped symbol stubs
  // alongside the T2 AAB. Materialize them deterministically when absent.
  const libDir = path.join(projectDir, 'lib', 'arm64-v8a');
  const sos = {
    'libengine-unstripped.so': 'unstripped-stub-not-real\n',
    'libui-unstripped.so': 'unstripped-stub-not-real\n',
  };
  for (const [name, content] of Object.entries(sos)) {
    const full = path.join(libDir, name);
    let stat = null;
    try {
      stat = fs.statSync(full);
    } catch {
      stat = null;
    }
    if (stat && stat.isFile()) continue;
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, content, 'utf8');
  }
  const texts = {
    'strip-map.txt': 'libengine-unstripped.so -> libengine.so\n',
    'mapping.ref': 'mapping-fake-not-real\n',
  };
  for (const [rel, content] of Object.entries(texts)) {
    const full = path.join(projectDir, rel);
    let stat = null;
    try {
      stat = fs.statSync(full);
    } catch {
      stat = null;
    }
    if (stat && stat.isFile()) continue;
    fs.writeFileSync(full, content, 'utf8');
  }
}

function runScaffoldSmoke(args) {
  const templateDir = args && args.templateDir;
  const workDir = args && args.workDir;
  const packName = args && args.packName;

  if (typeof workDir !== 'string' || workDir.length === 0) {
    throw new Error('smoke-failed: scaffold: invalid workDir');
  }

  const projectDir = path.join(workDir, 'app');

  // Stage 1 — scaffold.
  try {
    scaffoldProject({ templateDir, outDir: projectDir });
  } catch (err) {
    smokeFailed('scaffold', err);
  }

  // Stage 2 — signed AAB.
  let aabPath;
  try {
    ensureBuildInputs(projectDir);
    ensureSymbolInputs(projectDir);
    ({ aabPath } = buildSignedAab({ projectDir, keyAlias: SMOKE_KEY_ALIAS }));
  } catch (err) {
    smokeFailed('build', err);
  }

  // Stage 3 — delivery mount.
  let mountPoint;
  try {
    const { handle } = requestPack(packName);
    let terminal = null;
    for (let i = 0; i < 20; i += 1) {
      const cur = monitorPack(handle);
      if (cur.state === 'ready' || cur.state === 'failed') {
        terminal = cur;
        break;
      }
      terminal = cur;
    }
    if (!terminal || terminal.state !== 'ready') {
      throw new Error(
        `delivery-failed: pack not ready: ${terminal ? terminal.state : 'unknown'}`
      );
    }
    ({ mountPoint } = mountPack(handle));
  } catch (err) {
    smokeFailed('delivery', err);
  }

  // Stage 4 — symbols.
  let symbols;
  try {
    const collected = collectSymbols({ projectDir });
    symbols = collected.unstrippedSo;
  } catch (err) {
    smokeFailed('symbols', err);
  }

  return { ok: true, projectDir, aabPath, mountPoint, symbols };
}

module.exports = { runScaffoldSmoke };
