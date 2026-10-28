'use strict';
// T5 — FR-DX-008 / AC-DX-036 / AC-DX-037 / TC-DX-036 / TC-DX-037 (Batch 2).
// Hermetic: node:test + node:assert/strict only. All collaborators faked
// in-process (fake BUILD-001 impl, in-memory plugin registry, fs readdir of
// the lib dir for the TC-DX-037 boundary). No secrets, no config, no device.
// Interfaces under test (plan verbatim):
//   formatFailure(failure: {command: string, cause: string}) =>
//     {command: string, cause: string, action: string}
//   from tools/cli/lib/cli-errors.js;
//   registerPlugin(plugin: {name: string, command: string,
//     run: (args: string[]) => number}) => {name: string, command: string}
//   from tools/cli/lib/plugin-host.js;
//   tools/cli/lib/build-backend.js facade delegates to BUILD-001
//     (fake in tests).
// ASSUMPTION: invalid formatFailure input throws a TypeError (never returns
// a half-shaped error); duplicate plugin command registration is rejected
// with a structured error (never silently overwrites).
// ASSUMPTION: build-backend.js exposes either createBuildBackend(impl) or
// __setBuildImpl(impl) plus an invoke/build delegating method; the tests
// probe createBuildBackend first, then __setBuildImpl.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const LIB_DIR = path.resolve(__dirname, '..', 'lib');
const CLI_ERRORS = path.join(LIB_DIR, 'cli-errors.js');
const PLUGIN_HOST = path.join(LIB_DIR, 'plugin-host.js');
const BUILD_BACKEND = path.join(LIB_DIR, 'build-backend.js');

function fresh(modPath) {
  delete require.cache[modPath];
  return require(modPath);
}

describe('cli-errors formatFailure (T5 / TC-DX-036)', () => {
  it('formats {command, cause} into {command, cause, action}', () => {
    const { formatFailure } = fresh(CLI_ERRORS);
    const out = formatFailure({ command: 'build', cause: 'NDK path not set' });
    assert.equal(out.command, 'build');
    assert.equal(out.cause, 'NDK path not set');
    assert.equal(typeof out.action, 'string');
    assert.ok(out.action.length > 0, 'action must give a next step');
  });

  it('every error names command + cause + action as non-empty strings', () => {
    const { formatFailure } = fresh(CLI_ERRORS);
    for (const failure of [
      { command: 'doctor', cause: 'device offline' },
      { command: 'logcat', cause: 'transport closed' },
      { command: 'deeplink', cause: 'unknown route' },
    ]) {
      const out = formatFailure(failure);
      for (const key of ['command', 'cause', 'action']) {
        assert.equal(typeof out[key], 'string', `${key} must be a string`);
        assert.ok(out[key].length > 0, `${key} must be non-empty`);
      }
    }
  });

  it('error never requires engine source and never leaks a raw stack', () => {
    const { formatFailure } = fresh(CLI_ERRORS);
    const out = formatFailure({ command: 'build', cause: 'simulated failure' });
    const text = JSON.stringify(out);
    assert.ok(!/\n\s*at\s/.test(text), 'must not contain a raw stack trace');
    assert.ok(!/node_modules\/.+\.js:\d+/.test(text), 'must not contain frame paths');
    assert.ok(
      !/read the engine source|see engine source|internal source/i.test(text),
      'must never require engine source',
    );
  });

  it('missing command throws instead of returning a half-shaped error', () => {
    const { formatFailure } = fresh(CLI_ERRORS);
    assert.throws(() => formatFailure({ cause: 'no command' }), TypeError);
  });

  it('missing cause throws instead of returning a half-shaped error', () => {
    const { formatFailure } = fresh(CLI_ERRORS);
    assert.throws(() => formatFailure({ command: 'build' }), TypeError);
  });
});

describe('plugin-host registerPlugin (T5)', () => {
  beforeEach(() => {
    delete require.cache[PLUGIN_HOST];
  });

  it('registerPlugin returns {name, command}', () => {
    const { registerPlugin } = fresh(PLUGIN_HOST);
    const out = registerPlugin({ name: 'hello', command: 'hello', run: () => 0 });
    assert.equal(out.name, 'hello');
    assert.equal(out.command, 'hello');
  });

  it('plugin adds a command runnable without forking the CLI', () => {
    const mod = fresh(PLUGIN_HOST);
    const seen = [];
    mod.registerPlugin({
      name: 'greet',
      command: 'greet',
      run: (args) => {
        seen.push(args);
        return 0;
      },
    });
    let exit;
    if (typeof mod.dispatch === 'function') {
      exit = mod.dispatch('greet', ['--loud']);
    } else if (typeof mod.getPlugin === 'function') {
      exit = mod.getPlugin('greet').run(['--loud']);
    } else if (typeof mod.listCommands === 'function') {
      assert.ok(mod.listCommands().includes('greet'), 'plugin command must be listed');
      exit = mod.getPlugin('greet').run(['--loud']);
    } else {
      assert.fail('plugin-host must expose dispatch, getPlugin, or listCommands');
    }
    assert.equal(exit, 0);
    assert.deepEqual(seen, [['--loud']]);
  });

  it('plugin run return code propagates to the caller', () => {
    const mod = fresh(PLUGIN_HOST);
    mod.registerPlugin({ name: 'failer', command: 'failer', run: () => 3 });
    let exit;
    if (typeof mod.dispatch === 'function') exit = mod.dispatch('failer', []);
    else exit = mod.getPlugin('failer').run([]);
    assert.equal(exit, 3);
  });

  it('duplicate command registration is rejected, never silently overwritten', () => {
    const mod = fresh(PLUGIN_HOST);
    mod.registerPlugin({ name: 'dup', command: 'dup', run: () => 0 });
    assert.throws(
      () => mod.registerPlugin({ name: 'dup2', command: 'dup', run: () => 0 }),
      /already registered|duplicate/i,
    );
  });

  it('invalid plugin shape is rejected', () => {
    const { registerPlugin } = fresh(PLUGIN_HOST);
    assert.throws(() => registerPlugin({ name: 'x' }), TypeError);
  });
});

describe('build-backend facade delegates to BUILD-001 (T5)', () => {
  it('facade module loads without implementing packaging itself', () => {
    const mod = fresh(BUILD_BACKEND);
    assert.ok(mod && typeof mod === 'object');
    for (const key of ['package', 'packaging', 'bundleInternal', 'codegenInternal']) {
      assert.ok(!(key in mod), `facade must not expose build internals (${key})`);
    }
  });

  it('delegates invocation to the injected BUILD-001 fake and returns its result', async () => {
    const mod = fresh(BUILD_BACKEND);
    const calls = [];
    const fake = {
      build: async (opts) => {
        calls.push(opts);
        return { status: 'ok', artifact: '/fake/out.aab' };
      },
    };
    let invoke;
    let backend;
    if (typeof mod.createBuildBackend === 'function') {
      backend = mod.createBuildBackend(fake);
      invoke = backend.build || backend.invoke || backend.run;
    } else if (typeof mod.__setBuildImpl === 'function') {
      mod.__setBuildImpl(fake);
      invoke = mod.build || mod.invoke || mod.run;
      backend = mod;
    } else {
      assert.fail('build-backend must expose createBuildBackend(impl) or __setBuildImpl(impl)');
    }
    assert.equal(typeof invoke, 'function', 'facade must expose a delegating method');
    const out = await invoke.call(backend, { target: 'android' });
    assert.equal(calls.length, 1);
    assert.deepEqual(calls[0], { target: 'android' });
    assert.deepEqual(out, { status: 'ok', artifact: '/fake/out.aab' });
  });

  it('facade propagates BUILD-001 fake failures without rewriting them', async () => {
    const mod = fresh(BUILD_BACKEND);
    const fake = {
      build: async () => {
        throw new Error('fake BUILD-001 failure AC-DX-036');
      },
    };
    let invoke;
    let backend;
    if (typeof mod.createBuildBackend === 'function') {
      backend = mod.createBuildBackend(fake);
      invoke = backend.build || backend.invoke || backend.run;
    } else {
      mod.__setBuildImpl(fake);
      invoke = mod.build || mod.invoke || mod.run;
      backend = mod;
    }
    await assert.rejects(() => invoke.call(backend, {}), /fake BUILD-001 failure/);
  });
});

describe('TC-DX-037 boundary: no packaging sources under tools/cli/lib', () => {
  it('no tools/cli/lib/packaging* sources exist', () => {
    const entries = fs.readdirSync(LIB_DIR);
    const offenders = entries.filter((e) => e.startsWith('packaging'));
    assert.deepEqual(offenders, [], `packaging sources must not exist: ${offenders}`);
  });
});
