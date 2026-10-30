'use strict';
// @muse/cli — `muse-native init <dir>` project scaffolding.
// Copies the template tree (muse-native.yaml, package.json, tsconfig.json,
// src/App.tsx, README.md) into <dir>. Fails closed: missing operand is
// usage + exit 2, an existing non-empty dir is an error + exit 1 with no
// writes.
const fs = require('node:fs');
const path = require('node:path');

const TEMPLATES_DIR = path.join(__dirname, '..', 'templates');
const TEMPLATE_FILES = [
  'muse-native.yaml',
  'package.json',
  'tsconfig.json',
  'README.md',
  path.join('src', 'App.tsx'),
  path.join('proxy', 'proxy.config.json'),
  'Makefile',
  'AndroidManifest.xml',
  path.join('res', 'values', 'strings.xml'),
];

// D4: after copy, rewrite only the single `APP_NAME =` line in the
// scaffolded Makefile to the target directory's basename. All other
// files stay byte-equal to their template sources.
function rewriteMakefileAppName(makefilePath, targetDir) {
  const raw = fs.readFileSync(makefilePath, 'utf8');
  const base = path.basename(String(targetDir));
  const sanitized = base.replace(/[^A-Za-z0-9_.-]/g, '-');
  const name = sanitized.length > 0 ? sanitized : 'hello-app';
  const next = raw.replace(/^APP_NAME\s*=.*$/m, `APP_NAME = ${name}`);
  if (next !== raw) {
    fs.writeFileSync(makefilePath, next, 'utf8');
  }
}

function printInitUsage() {
  process.stderr.write('Usage: muse-native init <dir>\n');
}

function runInit(dir, opts) {
  if (!dir || (typeof dir === 'string' && dir.startsWith('-'))) {
    printInitUsage();
    process.exitCode = 2;
    return;
  }
  let entries = null;
  try {
    const stat = fs.statSync(dir);
    if (!stat.isDirectory()) {
      process.stderr.write(`Error: '${dir}' exists and is not a directory.\n`);
      process.exitCode = 1;
      return;
    }
    entries = fs.readdirSync(dir);
  } catch (err) {
    if (!err || err.code !== 'ENOENT') {
      process.stderr.write(`Error: cannot init '${dir}': ${err && err.message ? err.message : err}\n`);
      process.exitCode = 1;
      return;
    }
    entries = null;
  }
  const force = Boolean(opts && opts.force);
  if (entries !== null && entries.length > 0 && !force) {
    process.stderr.write(`Error: directory '${dir}' already exists and is not empty.\n`);
    process.exitCode = 1;
    return;
  }
  try {
    fs.mkdirSync(dir, { recursive: true });
    for (const rel of TEMPLATE_FILES) {
      const src = path.join(TEMPLATES_DIR, rel);
      const dest = path.join(dir, rel);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.copyFileSync(src, dest);
    }
    rewriteMakefileAppName(path.join(dir, 'Makefile'), dir);
  } catch (err) {
    // Best-effort rollback so no partial scaffold remains (mirrors
    // build.js's artefact cleanup); the error below is what matters.
    try {
      if (entries === null) {
        fs.rmSync(dir, { recursive: true, force: true });
      } else {
        for (const rel of TEMPLATE_FILES) {
          try {
            fs.rmSync(path.join(dir, rel), { force: true });
          } catch (_) {
            // Continue removing the remaining scaffold files.
          }
        }
        try {
          fs.rmSync(path.join(dir, 'src'), { recursive: true, force: true });
        } catch (_) {
          // src/ may not have been created; ignore.
        }
        try {
          fs.rmSync(path.join(dir, 'proxy'), { recursive: true, force: true });
        } catch (_) {
          // proxy/ may not have been created; ignore.
        }
        try {
          fs.rmSync(path.join(dir, 'res'), { recursive: true, force: true });
        } catch (_) {
          // res/ may not have been created; ignore.
        }
      }
    } catch (_) {
      // Best-effort cleanup; the error below is what matters.
    }
    process.stderr.write(`Error: cannot init '${dir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(`Scaffolded new muse-native project in ${dir}\n`);
  process.stdout.write(`Next steps:\n  cd ${dir}\n  muse-native build .\n  muse-native run .\n`);
  process.exitCode = 0;
}

function fail(reason) {
  throw new Error(`scaffold-failed: ${reason}`);
}

function scaffoldProject(args) {
  const templateDir = args && args.templateDir;
  const outDir = args && args.outDir;
  if (typeof templateDir !== 'string' || templateDir.length === 0) {
    fail('invalid templateDir');
  }
  if (typeof outDir !== 'string' || outDir.length === 0) {
    fail('invalid outDir');
  }
  // Route through the existing entry-point registry (create→init).
  let command = null;
  try {
    command = require('./entry-point.js').getEntryPointCommands('create');
  } catch (err) {
    fail(`registry lookup failed: ${err && err.message ? err.message : err}`);
  }
  if (command !== 'init') {
    fail(`unexpected create command: ${command}`);
  }
  let templateStat = null;
  try {
    templateStat = fs.statSync(templateDir);
  } catch (err) {
    fail(`template not found: ${err && err.code ? err.code : 'ENOENT'}`);
  }
  if (!templateStat.isDirectory()) {
    fail('template is not a directory');
  }
  let createdByUs = false;
  try {
    const stat = fs.statSync(outDir);
    if (!stat.isDirectory()) {
      fail('output exists and is not a directory');
    }
    const entries = fs.readdirSync(outDir);
    if (entries.length > 0) {
      fail('output directory already exists and is not empty');
    }
  } catch (err) {
    if (err && err.message && err.message.indexOf('scaffold-failed:') === 0) {
      throw err;
    }
    if (!err || err.code !== 'ENOENT') {
      fail(`cannot inspect output directory: ${err && err.message ? err.message : err}`);
    }
    createdByUs = true;
  }
  try {
    fs.mkdirSync(outDir, { recursive: true });
    for (const rel of TEMPLATE_FILES) {
      const src = path.join(templateDir, rel);
      const dest = path.join(outDir, rel);
      fs.mkdirSync(path.dirname(dest), { recursive: true });
      fs.copyFileSync(src, dest);
    }
    rewriteMakefileAppName(path.join(outDir, 'Makefile'), outDir);
  } catch (err) {
    if (err && err.message && err.message.indexOf('scaffold-failed:') === 0) {
      throw err;
    }
    try {
      if (createdByUs) {
        fs.rmSync(outDir, { recursive: true, force: true });
      } else {
        for (const rel of TEMPLATE_FILES) {
          try {
            fs.rmSync(path.join(outDir, rel), { force: true });
          } catch (_) {
            // Continue removing the remaining scaffold files.
          }
        }
        try {
          fs.rmSync(path.join(outDir, 'src'), { recursive: true, force: true });
        } catch (_) {
          // src/ may not have been created; ignore.
        }
        try {
          fs.rmSync(path.join(outDir, 'proxy'), { recursive: true, force: true });
        } catch (_) {
          // proxy/ may not have been created; ignore.
        }
        try {
          fs.rmSync(path.join(outDir, 'res'), { recursive: true, force: true });
        } catch (_) {
          // res/ may not have been created; ignore.
        }
      }
    } catch (_) {
      // Best-effort cleanup; the error below is what matters.
    }
    fail(`copy failed: ${err && err.code ? err.code : (err && err.message ? err.message : err)}`);
  }
  return { ok: true, projectDir: outDir };
}

module.exports = runInit;
module.exports.scaffoldProject = scaffoldProject;
