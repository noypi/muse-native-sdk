'use strict';
// @muse/cli — `muse-native run <appDir>` builds the pipeline in memory then
// launches (stub launcher; device deploy is a later lane).
// Build chains in memory via the shared loader + lowering (`lib/ir.js`):
// missing dir / unreadable sources / no `.tsx` sources / empty lowering →
// stderr + exit 1 without launching. No `dist/` coupling: nothing is read
// from or written to `<appDir>/dist` here.
// The local stub runs wherever run runs: the stub port is derived from the
// scaffolded `proxy/proxy.config.json` and the stub is started alongside
// the app, so a scaffolded app reaches `ready` with no key and no network.
// A stub port that cannot be bound is a hard error naming the port
// (stderr + exit non-zero, never a silent fallback to the network).
// Default target: host (documented here; device targets arrive later).
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const ir = require('./ir.js');

// Default launch target. Kept as a named constant so the launch line and a
// future device-deploy lane share one value.
const DEFAULT_TARGET = 'host';

// Default stub port. The T1 scaffold binds every route to
// `http://localhost:8787/stub/<route>`; configs without an explicit port
// fall back here.
const DEFAULT_STUB_PORT = 8787;

// Named-port bind failure for the stub. The returned error always names the
// port that could not be bound, so callers report it instead of hanging or
// falling back to the network silently.
function stubPortError(port) {
  return new Error(
    `cannot bind stub port ${port}: address already in use (no silent fallback to network)`,
  );
}

// Derive the stub port from `<projectDir>/proxy/proxy.config.json`: an
// explicit numeric `port` field wins, otherwise the first localhost stub
// endpoint port found in the route table, otherwise DEFAULT_STUB_PORT.
// Throws when the config cannot be read or parsed (callers fall back to the
// default so unrelated projects keep the existing launch behavior).
function resolveStubPort(projectDir) {
  const cfgPath = path.join(path.resolve(projectDir), 'proxy', 'proxy.config.json');
  const raw = fs.readFileSync(cfgPath, 'utf8');
  const cfg = JSON.parse(raw);
  if (cfg && Number.isInteger(cfg.port) && cfg.port > 0 && cfg.port < 65536) {
    return cfg.port;
  }
  const routes = (cfg && cfg.routes) || {};
  for (const name of Object.keys(routes)) {
    const text = JSON.stringify(routes[name]);
    const m = /(?:localhost|127\.0\.0\.1):(\d+)/.exec(text);
    if (m) {
      const port = Number(m[1]);
      if (Number.isInteger(port) && port > 0 && port < 65536) return port;
    }
  }
  return DEFAULT_STUB_PORT;
}

// Inline fallback with the same handler contract as `proxy/lib/stub.js`
// `handle(req)`: synchronous `{ status, body }`, throws on unknown routes.
// Used when the repo stub cannot be loaded (e.g. a scaffolded app running
// outside the checkout).
function inlineStubHandler() {
  const fixtures = {
    segment: { mask: 'polygon', encoding: 'polygon' },
    reason: { reasoning: 'recorded-spark-reason', chunks: ['{"t":"rec"}'] },
    structure: { structure: 'recorded-spark-structure', strict: true },
    transcribe: { text: 'recorded-transcript', chunks: ['a'] },
    'generate-image': { image: 'recorded-image-bytes', size: '1024x1024' },
  };
  return {
    handle(req) {
      const route = req && typeof req === 'object' ? req.route : undefined;
      if (typeof route !== 'string' || !Object.prototype.hasOwnProperty.call(fixtures, route)) {
        const err = new Error(`unknown-route: ${String(route)}`);
        err.code = 'unknown-route';
        err.status = 404;
        throw err;
      }
      return { status: 200, body: JSON.parse(JSON.stringify(fixtures[route])) };
    },
  };
}

function loadStubHandler() {
  try {
    const loaded = require('../../../proxy/lib/stub.js');
    if (loaded && typeof loaded.handle === 'function') return loaded;
  } catch (_) {
    // Scaffolded out-of-tree apps have no repo stub — fall through to the
    // inline handler with the same contract.
  }
  return inlineStubHandler();
}

// Start the scaffolded stub alongside the app on `port` (same handler
// contract as `proxy/lib/stub.js` `handle(req)`; routes served under
// `/stub/<route>` with no key and no network). A port that is already bound
// exits non-zero naming the port — never a hang, never a silent fallback
// to the network.
function startStub(projectDir, port) {
  const target = Number(port) || DEFAULT_STUB_PORT;
  const stub = loadStubHandler();
  void projectDir;
  const server = http.createServer((req, res) => {
    const url = req && typeof req.url === 'string' ? req.url : '';
    const m = /\/stub\/([^/?#]+)/.exec(url);
    const route = m ? decodeURIComponent(m[1]) : undefined;
    let result;
    try {
      result = stub.handle({ route, method: req.method, url });
    } catch (err) {
      const status = err && Number.isInteger(err.status) ? err.status : 404;
      res.writeHead(status, { 'content-type': 'application/json' });
      res.end(JSON.stringify({ error: (err && err.message) || 'stub error' }));
      return;
    }
    const status = result && Number.isInteger(result.status) ? result.status : 200;
    res.writeHead(status, { 'content-type': 'application/json' });
    res.end(JSON.stringify(result ? result.body : null));
  });
  server.on('error', (err) => {
    const failure = err && err.code === 'EADDRINUSE' ? stubPortError(target) : err;
    const detail = failure && failure.message ? failure.message : String(failure);
    try {
      process.stderr.write(`Error: ${detail}\n`);
    } catch (_) {
      // stderr gone — process.exit below still carries the status.
    }
    process.exit(1);
  });
  server.listen(target, '127.0.0.1');
  return server;
}

function runRun(appDir) {
  if (!appDir) {
    process.stderr.write('Error: missing <appDir>.\nUsage: muse-native run <appDir>\n');
    process.exitCode = 1;
    return;
  }
  const resolved = path.resolve(appDir);
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
  let sources;
  try {
    sources = ir.loadSources(resolved);
  } catch (err) {
    process.stderr.write(`Error: cannot read sources in '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  if (sources.length === 0) {
    process.stderr.write(`Error: no .tsx sources found in '${appDir}'.\n`);
    process.exitCode = 1;
    return;
  }
  try {
    const tree = ir.lowerToIR(sources);
    if (!tree || tree.version !== 1 || !Array.isArray(tree.roots) || tree.roots.length === 0) {
      throw new Error('lowering produced an empty tree');
    }
    // Full in-memory build pipeline: encoding must succeed too, even
    // though nothing is written to `dist/`.
    ir.encodeTree(tree);
  } catch (err) {
    process.stderr.write(`Error: cannot run '${appDir}': ${err && err.message ? err.message : err}\n`);
    process.exitCode = 1;
    return;
  }
  // The stub runs wherever run runs: derive the stub port from the
  // scaffolded proxy/proxy.config.json and start the stub alongside the
  // app, so the app reaches ready with no key and no network.
  let stubPort = DEFAULT_STUB_PORT;
  try {
    stubPort = resolveStubPort(resolved);
  } catch (_) {
    stubPort = DEFAULT_STUB_PORT;
  }
  try {
    startStub(resolved, stubPort);
  } catch (err) {
    const failure = err && /stub port/.test((err && err.message) || '') ? err : stubPortError(stubPort);
    process.stderr.write(`Error: ${failure.message}\n`);
    process.exitCode = 1;
    return;
  }
  process.stdout.write(`stub listening on 127.0.0.1:${stubPort}\n`);
  process.stdout.write(`launching ${appDir} (${DEFAULT_TARGET})\n`);
  process.exitCode = 0;
}

module.exports = runRun;
module.exports.run = runRun;
module.exports.stubPortError = stubPortError;
module.exports.resolveStubPort = resolveStubPort;
module.exports.startStub = startStub;
