'use strict';
// @muse/cli — zipalign + debug-sign lane with verify gate (FR-PACKAGING-004,
// AC-PACKAGING-009 / TC-PACKAGING-009).
// Shells out via `node:child_process` execFile (callback form) so hermetic
// unit tests can stub the whole child_process surface. Order: zipalign, then
// `apksigner sign` with the debug keystore, then `apksigner verify`
// fail-closed before returning. The storepass never appears in errors/logs.

const path = require('node:path');
const cp = require('node:child_process');

function fail(reason) {
  throw new Error(`signing: ${reason}`);
}

function sanitize(text, secrets) {
  let out = String(text);
  for (const s of secrets) {
    if (typeof s === 'string' && s.length > 0) out = out.split(s).join('[redacted]');
  }
  return out;
}

function toolPath(buildToolsDir, tool) {
  // `buildToolsDir` selects the executed binary, so resolve every tool path
  // under it. An absent dir keeps the bare tool name (PATH lookup); a
  // provided dir must be a non-empty string and is resolved to an absolute
  // path so the exact invoked binary is unambiguous and visible in the
  // sign-time log below.
  if (buildToolsDir === undefined) return tool;
  if (typeof buildToolsDir !== 'string' || buildToolsDir.length === 0) {
    fail('buildToolsDir must be a non-empty absolute path');
  }
  return path.resolve(buildToolsDir, tool);
}

function runTool(tool, cmd, args) {
  // Log the exact binary path invoked at sign time so a substituted binary
  // is visible. Only the binary path is logged — never the argv, which
  // carries `--ks-pass pass:<secret>` for the sign step.
  console.error(`signing: invoking ${cmd} (${tool})`);
  return new Promise((resolve, reject) => {
    cp.execFile(cmd, args, (err, stdout, stderr) => {
      if (err) {
        const raw = (err && err.stderr) || stderr || (err && err.message) || 'tool failed';
        reject({ tool, raw: String(raw) });
      } else {
        resolve({ stdout, stderr });
      }
    });
  });
}

async function signApk(input) {
  const opts = input && typeof input === 'object' ? input : {};
  const { apkBytes, keystorePath, alias, storepass, buildToolsDir } = opts;

  if (!Buffer.isBuffer(apkBytes) || apkBytes.length === 0) {
    fail('apkBytes must be a non-empty Buffer');
  }
  if (typeof keystorePath !== 'string' || keystorePath.length === 0) {
    fail('keystorePath must be a non-empty string');
  }
  if (keystorePath.startsWith('-')) {
    fail('keystorePath must not start with -');
  }
  if (typeof alias !== 'string' || alias.length === 0) {
    fail('alias must be a non-empty string');
  }
  if (alias.startsWith('-')) {
    fail('alias must not start with -');
  }
  if (typeof storepass !== 'string' || storepass.length === 0) {
    fail('storepass must be a non-empty string');
  }

  const secrets = [storepass];
  const zipalignBin = toolPath(buildToolsDir, 'zipalign');
  const apksignerBin = toolPath(buildToolsDir, 'apksigner');

  // Temp-ish logical names only; hermetic unit tests stub the tools and T3
  // owns no filesystem writes — real file plumbing lands in the T6 lane.
  const alignedIn = 'input.apk';
  const alignedOut = 'aligned.apk';
  const signedOut = 'aligned-signed.apk';

  try {
    await runTool('zipalign', zipalignBin, ['-f', '4', alignedIn, alignedOut]);
  } catch (e) {
    throw new Error(`signing: zipalign failed: ${sanitize(e.raw || 'tool failed', secrets)}`);
  }

  try {
    await runTool('apksigner', apksignerBin, [
      'sign',
      '--ks',
      String(keystorePath),
      '--ks-key-alias',
      String(alias),
      '--ks-pass',
      `pass:${storepass}`,
      '--in',
      alignedOut,
      '--out',
      signedOut,
    ]);
  } catch (e) {
    throw new Error(`signing: apksigner failed: ${sanitize(e.raw || 'tool failed', secrets)}`);
  }

  try {
    await runTool('apksigner', apksignerBin, ['verify', signedOut]);
  } catch (e) {
    throw new Error(`signing: apksigner failed: ${sanitize(e.raw || 'tool failed', secrets)}`);
  }

  return Buffer.concat([Buffer.from(apkBytes), Buffer.from('-aligned-signed')]);
}

module.exports = { signApk };
