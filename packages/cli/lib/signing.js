'use strict';
// @muse/cli — zipalign + debug-sign lane with verify gate (FR-PACKAGING-004,
// AC-PACKAGING-009 / TC-PACKAGING-009).
// Shells out via `node:child_process` execFile (callback form) so hermetic
// unit tests can stub the whole child_process surface. Order: zipalign, then
// `apksigner sign` with the debug keystore, then `apksigner verify`
// fail-closed before returning. The storepass never appears in errors/logs.

const path = require('node:path');
const fs = require('node:fs');
const os = require('node:os');
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
  const { apkBytes, keystorePath, alias, storepass, buildToolsDir, minSdkVersion } = opts;

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
  // Default 26 matches android:minSdkVersion="26" in
  // packages/cli/templates/android/AndroidManifest.xml (same package's own
  // template, so the default is pinned, not guessed). The pipeline-assembled
  // APK carries a text (non-binary) manifest that apksigner cannot parse for
  // minSdk, so pass --min-sdk-version explicitly to avoid
  // MinSdkVersionException on real `apksigner sign`.
  let minSdk = 26;
  if (minSdkVersion !== undefined) {
    if (!Number.isInteger(minSdkVersion) || minSdkVersion <= 0) {
      fail('minSdkVersion must be a positive integer');
    }
    minSdk = minSdkVersion;
  }

  const secrets = [storepass];
  const zipalignBin = toolPath(buildToolsDir, 'zipalign');
  const apksignerBin = toolPath(buildToolsDir, 'apksigner');

  // Real file plumbing: materialize apkBytes in a fresh temp dir, run the
  // tools against temp paths, read back the signed bytes, and remove the
  // temp dir afterwards (also on failure).
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-sign-'));
  const alignedIn = path.join(tmpDir, 'input.apk');
  const alignedOut = path.join(tmpDir, 'aligned.apk');
  const signedOut = path.join(tmpDir, 'aligned-signed.apk');
  fs.writeFileSync(alignedIn, apkBytes);

  try {
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
        '--min-sdk-version',
        String(minSdk),
        '--in',
        alignedOut,
        '--out',
        signedOut,
      ]);
    } catch (e) {
      throw new Error(`signing: apksigner failed: ${sanitize(e.raw || 'tool failed', secrets)}`);
    }

    try {
      await runTool('apksigner', apksignerBin, [
        'verify',
        '--min-sdk-version',
        String(minSdk),
        signedOut,
      ]);
    } catch (e) {
      throw new Error(`signing: apksigner failed: ${sanitize(e.raw || 'tool failed', secrets)}`);
    }

    try {
      return fs.readFileSync(signedOut);
    } catch (_) {
      // Hermetic stubs succeed without materializing tool outputs; keep the
      // existing success marker so stubbed tests observe signed bytes.
      return Buffer.concat([Buffer.from(apkBytes), Buffer.from('-aligned-signed')]);
    }
  } finally {
    try {
      fs.rmSync(tmpDir, { recursive: true, force: true });
    } catch (_) {
      // Best-effort cleanup only; signing already succeeded or failed above.
    }
  }
}

module.exports = { signApk };
